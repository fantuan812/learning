#!/usr/bin/env python3
"""SQLite fault-window teaching model; no production DB or distributed claims.

Only Python's standard library is used. All files are temporary. unittest
checks remain active with python -O. Negative controls deliberately show loss
of the invariant; their PASS is evidence of a reproduced defect.
"""
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, replace
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import threading
import unittest


@dataclass(frozen=True)
class Request:
    # In production these two fields must come from authenticated context.
    tenant: int = 1
    account: int = 10
    operation: str = "mall-buy-v1"
    key: str = "intent-A"
    sku: str = "potion"
    quantity: int = 1

    @property
    def scope(self):
        return self.tenant, self.account, self.operation, self.key

    @property
    def digest(self):
        # Deliberately small contract: integers and a catalog identifier only.
        payload = json.dumps({"version": 1, "sku": self.sku,
                              "quantity": self.quantity}, sort_keys=True,
                             separators=(",", ":"), allow_nan=False)
        return hashlib.sha256(payload.encode("utf-8")).hexdigest()


class KeyConflict(Exception):
    pass


class InjectedFailure(Exception):
    pass


def connect(path, timeout=5.0):
    # Explicit BEGIN/COMMIT, not sqlite3's implicit transaction defaults.
    return sqlite3.connect(path, timeout=timeout, isolation_level=None)


def initialize(path):
    db = connect(path)
    try:
        db.executescript("""
        PRAGMA journal_mode=DELETE;
        PRAGMA synchronous=FULL;
        CREATE TABLE wallet (
            tenant INTEGER, account INTEGER, balance INTEGER NOT NULL,
            items INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (tenant, account), CHECK(balance >= 0));
        CREATE TABLE idem (
            tenant INTEGER, account INTEGER, operation TEXT COLLATE BINARY,
            intent_key TEXT COLLATE BINARY, request_hash TEXT NOT NULL,
            status TEXT NOT NULL, snapshot TEXT,
            PRIMARY KEY (tenant, account, operation, intent_key));
        CREATE TABLE job (
            job_id INTEGER PRIMARY KEY, epoch INTEGER NOT NULL,
            status TEXT NOT NULL);
        INSERT INTO wallet VALUES (1, 10, 1000, 0), (1, 11, 1000, 0),
                                  (2, 10, 1000, 0);
        INSERT INTO job VALUES (1, 1, 'PROCESSING');
        """)
    finally:
        db.close()


def purchase(path, req=Request(), hook=lambda point: None, timeout=5.0):
    if not isinstance(req.key, str) or not req.key or len(req.key) > 128:
        raise ValueError("missing or invalid required idempotency key")
    if req.sku != "potion" or type(req.quantity) is not int or not 1 <= req.quantity <= 20:
        raise ValueError("invalid normalized request")
    # Price is authoritative catalog data in this toy model, never client input.
    cost = 100 * req.quantity
    db = connect(path, timeout)
    try:
        # SQLite serializes writers. This is NOT PostgreSQL row-lock behavior.
        db.execute("BEGIN IMMEDIATE")
        inserted = db.execute("""INSERT INTO idem VALUES (?, ?, ?, ?, ?, 'PROCESSING', NULL)
            ON CONFLICT(tenant, account, operation, intent_key) DO NOTHING""",
                              (*req.scope, req.digest)).rowcount
        if not inserted:
            row = db.execute("""SELECT request_hash, status, snapshot FROM idem
                WHERE tenant=? AND account=? AND operation=? AND intent_key=?""",
                             req.scope).fetchone()
            if row is None:
                raise RuntimeError("missing conflict record")
            if row[0] != req.digest:
                raise KeyConflict("same scoped key with different business parameters")
            if row[1] not in ("SUCCEEDED", "REJECTED") or row[2] is None:
                raise RuntimeError("non-terminal record requires explicit recovery")
            db.execute("COMMIT")
            return row[2]

        debited = db.execute("""UPDATE wallet SET balance=balance-?
            WHERE tenant=? AND account=? AND balance>=?""",
                             (cost, req.tenant, req.account, cost)).rowcount
        if debited:
            hook("after_debit")
            db.execute("UPDATE wallet SET items=items+? WHERE tenant=? AND account=?",
                       (req.quantity, req.tenant, req.account))
            state = "SUCCEEDED"
        else:
            state = "REJECTED"  # This example deliberately persists insufficient funds.
        wallet = db.execute("SELECT balance, items FROM wallet WHERE tenant=? AND account=?",
                            (req.tenant, req.account)).fetchone()
        if wallet is None:
            raise ValueError("account does not exist")
        snapshot = json.dumps({"state": state, "balance": wallet[0], "items": wallet[1]},
                              sort_keys=True, separators=(",", ":"))
        db.execute("""UPDATE idem SET status=?, snapshot=?
            WHERE tenant=? AND account=? AND operation=? AND intent_key=?""",
                   (state, snapshot, *req.scope))
        hook("before_commit")
        db.execute("COMMIT")
        hook("after_commit")
        return snapshot
    except BaseException:
        if db.in_transaction:
            db.execute("ROLLBACK")
        raise
    finally:
        db.close()


def unsafe_business_then_record(path, crash_before_record=False):
    """Negative control: business and dedup are two independent commits."""
    db = connect(path)
    try:
        if db.execute("SELECT 1 FROM idem").fetchone():
            return
        db.execute("UPDATE wallet SET balance=balance-100, items=items+1 WHERE tenant=1 AND account=10")
        if crash_before_record:
            raise InjectedFailure("business committed, record missing")
        db.execute("INSERT INTO idem VALUES (1,10,'mall-buy-v1','intent-A',?,'SUCCEEDED','unsafe')",
                   (Request().digest,))
    finally:
        db.close()


def finalize_job(path, epoch, fail=False):
    """Resource-side epoch AND phase check in the same transaction as effect."""
    db = connect(path)
    try:
        db.execute("BEGIN IMMEDIATE")
        updated = db.execute("""UPDATE job SET status='SUCCEEDED'
            WHERE job_id=1 AND epoch=? AND status='PROCESSING'""", (epoch,)).rowcount
        if updated:
            db.execute("UPDATE wallet SET items=items+1 WHERE tenant=1 AND account=10")
            if fail:
                raise InjectedFailure("after guarded effect")
        db.execute("COMMIT")
        return bool(updated)
    except BaseException:
        if db.in_transaction:
            db.execute("ROLLBACK")
        raise
    finally:
        db.close()


class IdempotencyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="learning-idem-")
        self.addCleanup(self.temp.cleanup)
        self.path = str(Path(self.temp.name) / "test.sqlite")
        initialize(self.path)

    def query(self, sql, parameters=()):
        db = connect(self.path)
        try:
            return db.execute(sql, parameters).fetchall()
        finally:
            db.close()

    def state(self, tenant=1, account=10):
        return self.query("SELECT balance,items FROM wallet WHERE tenant=? AND account=?",
                          (tenant, account))[0]

    def test_01_first_commit_and_same_key_replay(self):
        first = purchase(self.path)
        self.assertEqual(purchase(self.path), first)
        self.assertEqual(self.state(), (900, 1))
        self.assertEqual(self.query("SELECT status FROM idem"), [("SUCCEEDED",)])

    def test_02_same_key_changed_payload_is_conflict(self):
        purchase(self.path)
        with self.assertRaises(KeyConflict):
            purchase(self.path, replace(Request(), quantity=2))
        self.assertEqual(self.state(), (900, 1))

    def test_03_authenticated_account_scope_isolated(self):
        purchase(self.path)
        purchase(self.path, replace(Request(), account=11))
        self.assertEqual(self.state(), (900, 1))
        self.assertEqual(self.state(account=11), (900, 1))
        self.assertEqual(self.query("SELECT count(*) FROM idem"), [(2,)])

    def test_04_tenant_scope_isolated(self):
        purchase(self.path)
        purchase(self.path, replace(Request(), tenant=2))
        self.assertEqual(self.state(tenant=2), (900, 1))
        self.assertEqual(self.query("SELECT count(*) FROM idem"), [(2,)])

    def test_05_new_intent_is_a_new_purchase(self):
        purchase(self.path)
        purchase(self.path, replace(Request(), key="intent-B"))
        self.assertEqual(self.state(), (800, 2))

    def test_06_rejection_snapshot_does_not_change_after_topup(self):
        req = replace(Request(), quantity=11)
        first = purchase(self.path, req)
        self.assertEqual(json.loads(first)["state"], "REJECTED")
        self.query("UPDATE wallet SET balance=2000 WHERE tenant=1 AND account=10")
        self.assertEqual(purchase(self.path, req), first)
        self.assertEqual(self.state(), (2000, 0))
        purchase(self.path, replace(req, key="intent-B"))
        self.assertEqual(self.state(), (900, 11))

    def test_07_failure_after_debit_rolls_back_everything(self):
        def hook(point):
            if point == "after_debit":
                raise InjectedFailure(point)
        with self.assertRaises(InjectedFailure):
            purchase(self.path, hook=hook)
        self.assertEqual(self.state(), (1000, 0))
        self.assertEqual(self.query("SELECT count(*) FROM idem"), [(0,)])
        purchase(self.path)
        self.assertEqual(self.state(), (900, 1))

    def crash_worker(self, point):
        args = [sys.executable]
        if sys.flags.optimize:
            args.append("-O")
        process = subprocess.run([*args, str(Path(__file__).resolve()), "--crash-worker",
                                  self.path, point], capture_output=True, timeout=10)
        self.assertEqual(process.returncode, 86, process.stderr.decode())

    def test_08_process_exit_before_commit_leaves_no_effect(self):
        self.crash_worker("before_commit")
        self.assertEqual(self.state(), (1000, 0))
        self.assertEqual(self.query("SELECT count(*) FROM idem"), [(0,)])
        purchase(self.path)
        self.assertEqual(self.state(), (900, 1))

    def test_09_process_exit_after_commit_replays_saved_result(self):
        self.crash_worker("after_commit")
        self.assertEqual(self.state(), (900, 1))
        saved = self.query("SELECT snapshot FROM idem")[0][0]
        self.assertEqual(purchase(self.path), saved)
        self.assertEqual(self.state(), (900, 1))

    def test_10_inflight_writer_is_busy_not_permission_to_duplicate(self):
        entered, release = threading.Event(), threading.Event()
        def hook(point):
            if point == "after_debit":
                entered.set()
                if not release.wait(timeout=5):
                    raise RuntimeError("test synchronization timed out")
        with ThreadPoolExecutor(max_workers=1) as pool:
            future = pool.submit(purchase, self.path, hook=hook)
            try:
                self.assertTrue(entered.wait(timeout=5))
                with self.assertRaises(sqlite3.OperationalError) as caught:
                    purchase(self.path, timeout=0.0)
                self.assertEqual(caught.exception.sqlite_errorcode, sqlite3.SQLITE_BUSY)
            finally:
                release.set()
            first = future.result(timeout=5)
        self.assertEqual(purchase(self.path), first)
        self.assertEqual(self.state(), (900, 1))

    def test_11_eight_concurrent_connections_commit_one_effect(self):
        barrier = threading.Barrier(8)
        def call(_):
            barrier.wait(timeout=5)
            return purchase(self.path)
        with ThreadPoolExecutor(max_workers=8) as pool:
            replies = list(pool.map(call, range(8)))
        self.assertEqual(len(set(replies)), 1)
        self.assertEqual(self.state(), (900, 1))
        self.assertEqual(self.query("SELECT count(*) FROM idem"), [(1,)])

    def test_12_negative_control_split_commit_duplicates(self):
        with self.assertRaises(InjectedFailure):
            unsafe_business_then_record(self.path, crash_before_record=True)
        self.assertEqual(self.state(), (900, 1))
        self.assertEqual(self.query("SELECT count(*) FROM idem"), [(0,)])
        unsafe_business_then_record(self.path)
        self.assertEqual(self.state(), (800, 2))  # Defect reproduced, not a desired result.

    def test_13_negative_control_pruning_all_memory_allows_old_key(self):
        purchase(self.path)
        self.query("DELETE FROM idem")  # Simulated unsafe expiry, no tombstone/order key.
        purchase(self.path)
        self.assertEqual(self.state(), (800, 2))

    def test_14_stale_epoch_rejected_and_current_epoch_only_once(self):
        self.assertEqual(self.query("UPDATE job SET epoch=2 WHERE job_id=1 AND epoch=1 RETURNING epoch"), [(2,)])
        self.assertFalse(finalize_job(self.path, 1))
        self.assertEqual(self.state(), (1000, 0))
        self.assertTrue(finalize_job(self.path, 2))
        self.assertFalse(finalize_job(self.path, 2))
        self.assertEqual(self.state(), (1000, 1))

    def test_15_fencing_phase_and_effect_roll_back_together(self):
        with self.assertRaises(InjectedFailure):
            finalize_job(self.path, 1, fail=True)
        self.assertEqual(self.query("SELECT status FROM job"), [("PROCESSING",)])
        self.assertEqual(self.state(), (1000, 0))
        self.assertTrue(finalize_job(self.path, 1))
        self.assertEqual(self.state(), (1000, 1))

    def test_16_invalid_request_does_not_create_record(self):
        for quantity in (0, -1, 21, True, 1.5):
            with self.assertRaises(ValueError):
                purchase(self.path, replace(Request(), quantity=quantity))
        self.assertEqual(self.query("SELECT count(*) FROM idem"), [(0,)])
        self.assertEqual(self.state(), (1000, 0))


    def test_17_operation_scope_isolated(self):
        purchase(self.path)
        purchase(self.path, replace(Request(), operation="mall-buy-v2"))
        self.assertEqual(self.state(), (800, 2))
        self.assertEqual(self.query("SELECT count(*) FROM idem"), [(2,)])

    def test_18_committed_processing_is_not_reexecuted(self):
        self.query("INSERT INTO idem VALUES (1,10,'mall-buy-v1','intent-A',?,'PROCESSING',NULL)",
                   (Request().digest,))
        with self.assertRaisesRegex(RuntimeError, "non-terminal"):
            purchase(self.path)
        self.assertEqual(self.state(), (1000, 0))
        self.assertEqual(self.query("SELECT status FROM idem"), [("PROCESSING",)])

    def test_19_missing_required_key_cannot_bypass_gate(self):
        for key in ("", None, "x" * 129):
            with self.assertRaises(ValueError):
                purchase(self.path, replace(Request(), key=key))
        self.assertEqual(self.state(), (1000, 0))
        self.assertEqual(self.query("SELECT count(*) FROM idem"), [(0,)])


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--crash-worker":
        def exit_at(point):
            if point == sys.argv[3]:
                os._exit(86)  # No Python finally/connection cleanup in the child.
        purchase(sys.argv[2], hook=exit_at)
        raise SystemExit("requested crash point not reached")
    print(f"python={sys.version.split()[0]} sqlite={sqlite3.sqlite_version} optimize={sys.flags.optimize}", flush=True)
    unittest.main(verbosity=2)
