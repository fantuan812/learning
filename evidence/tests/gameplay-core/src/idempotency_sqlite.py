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


class StorageContractError(RuntimeError):
    """The transaction cannot publish the promised single terminal record."""


def validate_request(req):
    # Authentication is the caller's job. These are representation checks, not
    # proof that a caller owns a tenant/account. Do not coerce bool/float/str IDs.
    for name in ("tenant", "account"):
        value = getattr(req, name)
        if type(value) is not int or not -(2**63) <= value < 2**63:
            raise ValueError(f"{name} must be an exact signed-64-bit int")
    for name in ("operation", "key"):
        value = getattr(req, name)
        if not isinstance(value, str) or not value:
            raise ValueError(f"{name} must be a nonempty str")
        if name == "key" and len(value) > 128:
            raise ValueError("idempotency key exceeds 128 Python characters")
        try:
            value.encode("utf-8")
        except UnicodeEncodeError as exc:
            raise ValueError(f"{name} must be UTF-8 encodable") from exc
    # No strip/lower/Unicode normalization/truncation: BINARY key equality.
    # Zero/negative IDs are representable; existence is a separate DB check.
    if req.sku != "potion" or type(req.quantity) is not int or not 1 <= req.quantity <= 20:
        raise ValueError("invalid normalized request")


def connect(path, timeout=5.0):
    # Python 3.12 introduced autocommit; pin legacy control when available so
    # isolation_level=None keeps explicit SQL BEGIN/COMMIT even if defaults change.
    options = {"autocommit": sqlite3.LEGACY_TRANSACTION_CONTROL} if hasattr(
        sqlite3, "LEGACY_TRANSACTION_CONTROL") else {}
    return sqlite3.connect(path, timeout=timeout, isolation_level=None, **options)


def initialize(path):
    db = connect(path)
    try:
        db.executescript("""
        PRAGMA journal_mode=DELETE;
        PRAGMA synchronous=FULL;
        CREATE TABLE wallet (
            tenant INTEGER NOT NULL, account INTEGER NOT NULL, balance INTEGER NOT NULL,
            items INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (tenant, account), CHECK(balance >= 0));
        CREATE TABLE idem (
            tenant INTEGER NOT NULL, account INTEGER NOT NULL,
            operation TEXT COLLATE BINARY NOT NULL,
            intent_key TEXT COLLATE BINARY NOT NULL, request_hash TEXT NOT NULL,
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
    validate_request(req)  # Entire scope and payload, before opening a connection.
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
        terminal_rows = db.execute("""UPDATE idem SET status=?, snapshot=?
            WHERE tenant=? AND account=? AND operation=? AND intent_key=?
              AND status='PROCESSING' AND snapshot IS NULL""",
                                   (state, snapshot, *req.scope)).rowcount
        if terminal_rows != 1:
            raise StorageContractError(f"terminal update affected {terminal_rows} rows; expected exactly 1")
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


    def assert_invalid_before_connect(self, req):
        unopened = str(Path(self.temp.name) / "must-not-be-created.sqlite")
        with self.assertRaises(ValueError):
            purchase(unopened, req)
        self.assertFalse(Path(unopened).exists())
        self.assertEqual(self.state(), (1000, 0))
        self.assertEqual(self.query("SELECT * FROM idem"), [])

    def test_20_invalid_operation_rejected_before_connection(self):
        for operation in (None, "", 1, True, 1.0, b"mall-buy-v1", "\ud800"):
            with self.subTest(operation=repr(operation)):
                self.assert_invalid_before_connect(replace(Request(), operation=operation))
        # The original operation=None defect must reject on every retry.
        for _ in range(2):
            with self.assertRaises(ValueError):
                purchase(self.path, replace(Request(), operation=None))
        self.assertEqual(self.state(), (1000, 0))
        self.assertEqual(self.query("SELECT * FROM idem"), [])

    def test_21_identity_is_exact_int_in_sqlite_signed64_domain(self):
        class IntSubclass(int):
            pass
        for field in ("tenant", "account"):
            for value in (None, True, False, 1.0, "1", b"1", IntSubclass(1),
                          -(2**63)-1, 2**63):
                with self.subTest(field=field, value=repr(value)):
                    self.assert_invalid_before_connect(replace(Request(), **{field: value}))

    def test_22_signed64_endpoints_zero_and_negative_are_representable(self):
        for field in ("tenant", "account"):
            for value in (-(2**63), -1, 0, 2**63-1):
                with self.subTest(field=field, value=value):
                    req = replace(Request(), **{field: value})
                    self.query("INSERT INTO wallet VALUES (?,?,1000,0)", req.scope[:2])
                    first = purchase(self.path, req)
                    self.assertEqual(purchase(self.path, req), first)
                    self.assertEqual(self.state(*req.scope[:2]), (900, 1))
        # Representable does not mean provisioned, and is not authentication.
        with self.assertRaisesRegex(ValueError, "account does not exist"):
            purchase(self.path, replace(Request(), account=99))
        self.assertEqual(self.query("SELECT count(*) FROM idem"), [(8,)])

    def test_23_key_equality_preserves_case_space_unicode_and_nul(self):
        keys = ("A", "a", " A ", " ", "é", "e\u0301", "x"*128, "🧪"*128, "a\x00b")
        for key in keys:
            with self.subTest(key=repr(key)):
                req = replace(Request(), key=key)
                first = purchase(self.path, req)
                self.assertEqual(purchase(self.path, req), first)
        self.assertEqual(self.state(), (1000-100*len(keys), len(keys)))
        self.assertEqual({row[0] for row in self.query("SELECT intent_key FROM idem")}, set(keys))

    def test_24_operation_is_nonempty_without_silent_normalization(self):
        for operation in ("mall-buy-v1", "mall-buy-v2", "Mall-buy-v1", " "):
            req = replace(Request(), operation=operation)
            first = purchase(self.path, req)
            self.assertEqual(purchase(self.path, req), first)
        self.assertEqual(self.state(), (600, 4))
        self.assertEqual(self.query("SELECT count(*) FROM idem"), [(4,)])

    def test_25_schema_rejects_null_for_all_six_scope_columns(self):
        purchase(self.path)
        for table, columns, base in (
            ("wallet", ("tenant", "account"), [3, 20, 1000, 0]),
            ("idem", ("tenant", "account", "operation", "intent_key"),
             [3, 20, "mall-buy-v1", "direct", Request().digest, "PROCESSING", None]),
        ):
            schema = {row[1]: row[3] for row in self.query(f"PRAGMA table_info({table})")}
            for index, column in enumerate(columns):
                with self.subTest(table=table, column=column):
                    self.assertEqual(schema[column], 1)
                    values = base.copy()
                    values[index] = None
                    with self.assertRaises(sqlite3.IntegrityError):
                        self.query(f"INSERT INTO {table} VALUES ({','.join('?' for _ in values)})", values)
                    with self.assertRaises(sqlite3.IntegrityError):
                        self.query(f"UPDATE {table} SET {column}=NULL")
        self.assertEqual(self.state(), (900, 1))
        self.assertEqual(self.query("SELECT count(*) FROM wallet"), [(3,)])
        self.assertEqual(self.query("SELECT status FROM idem"), [("SUCCEEDED",)])

    def test_26_real_trigger_zero_terminal_rows_rolls_back_success_and_rejection(self):
        # RAISE(IGNORE) on this isolated test DB is real SQLite rowcount=0,
        # not a mocked cursor. It models a broken storage-side contract.
        self.query("""CREATE TRIGGER suppress_terminal BEFORE UPDATE OF status ON idem
            BEGIN SELECT RAISE(IGNORE); END""")
        for quantity in (1, 11):
            with self.subTest(quantity=quantity):
                with self.assertRaisesRegex(StorageContractError, "affected 0 rows"):
                    purchase(self.path, replace(Request(), quantity=quantity))
                self.assertEqual(self.state(), (1000, 0))
                self.assertEqual(self.query("SELECT * FROM idem"), [])
        self.query("DROP TRIGGER suppress_terminal")
        first = purchase(self.path)
        self.assertEqual(purchase(self.path), first)
        self.assertEqual(self.state(), (900, 1))

    def test_27_real_terminal_sql_error_rolls_back_effect_and_placeholder(self):
        self.query("""CREATE TRIGGER fail_terminal BEFORE UPDATE OF status ON idem
            BEGIN SELECT RAISE(ABORT, 'terminal failure'); END""")
        with self.assertRaisesRegex(sqlite3.IntegrityError, "terminal failure"):
            purchase(self.path)
        self.assertEqual(self.state(), (1000, 0))
        self.assertEqual(self.query("SELECT * FROM idem"), [])

    def test_28_hook_new_connection_reentry_before_and_after_commit(self):
        def reenter_before(point):
            if point == "after_debit":
                purchase(self.path, timeout=0)
        with self.assertRaises(sqlite3.OperationalError) as caught:
            purchase(self.path, hook=reenter_before)
        self.assertEqual(caught.exception.sqlite_errorcode, sqlite3.SQLITE_BUSY)
        self.assertEqual(self.state(), (1000, 0))
        self.assertEqual(self.query("SELECT * FROM idem"), [])
        replies = []
        def reenter_after(point):
            if point == "after_commit":
                replies.append(purchase(self.path, timeout=0))
        first = purchase(self.path, hook=reenter_after)
        self.assertEqual(replies, [first])
        self.assertEqual(self.state(), (900, 1))

    def test_29_explicit_sql_transaction_control(self):
        db = connect(self.path)
        try:
            self.assertIsNone(db.isolation_level)
            if hasattr(sqlite3, "LEGACY_TRANSACTION_CONTROL"):
                self.assertEqual(db.autocommit, sqlite3.LEGACY_TRANSACTION_CONTROL)
            self.assertFalse(db.in_transaction)
            db.execute("BEGIN IMMEDIATE")
            self.assertTrue(db.in_transaction)
            db.execute("ROLLBACK")
            self.assertFalse(db.in_transaction)
        finally:
            db.close()

    def test_30_invalid_key_and_payload_rejected_before_connection(self):
        for key in ("", None, 1, True, 1.0, b"intent", "x"*129, "🧪"*129, "\ud800"):
            with self.subTest(key=repr(key)):
                self.assert_invalid_before_connect(replace(Request(), key=key))
        for quantity in (0, -1, 21, True, 1.0, "1", None):
            with self.subTest(quantity=repr(quantity)):
                self.assert_invalid_before_connect(replace(Request(), quantity=quantity))
        self.assert_invalid_before_connect(replace(Request(), sku="unknown"))


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--crash-worker":
        def exit_at(point):
            if point == sys.argv[3]:
                os._exit(86)  # No Python finally/connection cleanup in the child.
        purchase(sys.argv[2], hook=exit_at)
        raise SystemExit("requested crash point not reached")
    print(f"python={sys.version.split()[0]} sqlite={sqlite3.sqlite_version} optimize={sys.flags.optimize}", flush=True)
    unittest.main(verbosity=2)
