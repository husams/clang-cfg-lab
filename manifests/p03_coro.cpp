// Part 3.7 -- a coroutine (needs -std=c++20; declares its own tiny <coroutine>).
namespace std {
template <class R, class... A> struct coroutine_traits { using promise_type = typename R::promise_type; };
template <class P = void> struct coroutine_handle {
  static coroutine_handle from_address(void *);
  static coroutine_handle from_promise(P &);
  void resume();
};
template <> struct coroutine_handle<void> {
  static coroutine_handle from_address(void *);
  template <class P> coroutine_handle(coroutine_handle<P>);
  coroutine_handle();
};
struct suspend_never {
  bool await_ready() noexcept;
  void await_suspend(coroutine_handle<>) noexcept;
  void await_resume() noexcept;
};
} // namespace std

struct Task {
  struct promise_type {
    Task get_return_object();
    std::suspend_never initial_suspend() noexcept;
    std::suspend_never final_suspend() noexcept;
    void return_void();
    void unhandled_exception();
  };
};

Task coro(int n) {
  if (n)
    co_return;
  co_await std::suspend_never{};
}
