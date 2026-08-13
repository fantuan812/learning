// move_counter.cpp — Copy/Move 计数实验（证据：evidence/labs/cpp-move）
//
// 实验问题：
//  1) std::vector 扩容时元素是复制还是移动？为什么 noexcept 移动决定扩容成本？
//  2) reserve(N) 能否消除扩容期的复制/移动？
//  3) C++17 保证的 copy elision（RVO/NRVO）在什么条件下发生？return std::move(t) 为什么是反模式？
//  4) moved-from 对象处于什么状态？
//
// 构建：cl /nologo /O2 /std:c++17 /EHsc /W4 move_counter.cpp /Fe:move_counter.exe
// 运行：move_counter.exe（输出即为原始结果，保存到 results/）

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

// 同时提供拷贝与 noexcept 移动的类型：vector 扩容时按 noexcept 移动元素
struct Moveable {
    static int copies;
    static int moves;
    static int ctors;
    std::string payload;
    Moveable() : payload(64, 'x') { ++ctors; }
    Moveable(const Moveable& o) : payload(o.payload) { ++copies; }
    Moveable(Moveable&& o) noexcept : payload(std::move(o.payload)) { ++moves; }
    Moveable& operator=(const Moveable& o) { payload = o.payload; ++copies; return *this; }
    Moveable& operator=(Moveable&& o) noexcept { payload = std::move(o.payload); ++moves; return *this; }
};
int Moveable::copies = 0;
int Moveable::moves = 0;
int Moveable::ctors = 0;

// 只有拷贝、没有移动的类型（用户声明了拷贝构造/赋值，移动构造被抑制）：
// vector 扩容时只能逐元素复制
struct CopyOnly {
    static int copies;
    static int ctors;
    std::string payload;
    CopyOnly() : payload(64, 'x') { ++ctors; }
    CopyOnly(const CopyOnly& o) : payload(o.payload) { ++copies; }
    CopyOnly& operator=(const CopyOnly& o) { payload = o.payload; ++copies; return *this; }
};
int CopyOnly::copies = 0;
int CopyOnly::ctors = 0;

// NRVO：返回具名局部对象（非强制，编译器优化）
static Moveable make_nrvo() {
    Moveable t;   // 具名对象
    return t;     // NRVO：应省略拷贝/移动
}

// 返回临时对象：C++17 起保证 elision（guaranteed copy elision）
static Moveable make_prvalue() {
    return Moveable();
}

// 反模式：return std::move(t) 强制移动，破坏 NRVO
static Moveable make_move_forced() {
    Moveable t;
    return std::move(t);
}

int main() {
    using std::printf;

    printf("== 1) vector 扩容：noexcept 移动类型（先 reserve(1)，再 push_back 10000 次）==\n");
    {
        std::vector<Moveable> v;
        v.reserve(1);
        const int N = 10000;
        for (int i = 0; i < N; ++i) {
            v.push_back(Moveable());   // 临时对象 → 移动构造入槽；扩容时整体搬移
        }
        printf("push_back x%d: capacity=%zu, ctors=%d, copies=%d, moves=%d\n",
               N, v.capacity(), Moveable::ctors, Moveable::copies, Moveable::moves);
    }

    printf("\n== 2) 相同操作但先 reserve(N)：扩容消失 ==\n");
    {
        Moveable::ctors = Moveable::copies = Moveable::moves = 0;
        std::vector<Moveable> v;
        const int N = 10000;
        v.reserve(N);
        for (int i = 0; i < N; ++i) {
            v.push_back(Moveable());
        }
        printf("push_back x%d (reserve): capacity=%zu, ctors=%d, copies=%d, moves=%d\n",
               N, v.capacity(), Moveable::ctors, Moveable::copies, Moveable::moves);
    }

    printf("\n== 3) 只有拷贝的类型（无移动构造）：扩容退化为逐元素复制 ==\n");
    {
        std::vector<CopyOnly> v;
        const int N = 10000;
        v.reserve(1);
        for (int i = 0; i < N; ++i) {
            v.push_back(CopyOnly());
        }
        printf("push_back x%d (CopyOnly): capacity=%zu, ctors=%d, copies=%d\n",
               N, v.capacity(), CopyOnly::ctors, CopyOnly::copies);
    }

    printf("\n== 4) 返回值优化 ==\n");
    {
        Moveable::ctors = Moveable::copies = Moveable::moves = 0;
        Moveable a = make_nrvo();
        printf("NRVO 返回具名对象: ctors=%d, copies=%d, moves=%d\n",
               Moveable::ctors, Moveable::copies, Moveable::moves);

        Moveable::ctors = Moveable::copies = Moveable::moves = 0;
        Moveable b = make_prvalue();
        printf("C++17 返回临时对象: ctors=%d, copies=%d, moves=%d\n",
               Moveable::ctors, Moveable::copies, Moveable::moves);

        Moveable::ctors = Moveable::copies = Moveable::moves = 0;
        Moveable c = make_move_forced();
        printf("反模式 return std::move(t): ctors=%d, copies=%d, moves=%d\n",
               Moveable::ctors, Moveable::copies, Moveable::moves);
    }

    printf("\n== 5) moved-from 状态（长字符串超过 SSO 长度，移动才会真正转移缓冲区）==\n");
    {
        std::string s = "hello world, this is a long string beyond the SSO threshold";
        std::string t = std::move(s);
        printf("move 后: t.size()=%zu, s.size()=%zu（实现定义：MSVC 下通常为空，但必须保持有效、可析构、可赋值）\n",
               t.size(), s.size());
    }

    return 0;
}
