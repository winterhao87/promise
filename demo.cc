#include "promise.h"
#include <iostream>
#include <assert.h>

class TestException : public std::exception {
public:
  TestException(const char *s) : _msg(s) {}

  const char* what() const noexcept override { return _msg; }

private:
  const char *_msg{};
};

int main(int argc, char *argv[]) {
  int age {100};
  std::string name {"PromiseDemo"};
  auto fut = async::MakeReadyPromise(std::move(age), std::move(name));
  fut.Then([](int &&num, std::string &&str) {
    assert(num == 100);
    assert(str == "PromiseDemo");
    return async::MakeReadyPromise(100, 200);
  })
  .Then([](int &&x, int &&y) {
    assert(x == 100);
    assert(y == 200);
    return async::MakeException<int>(new TestException("NonExcep"));
  })
  .Then([](int &&num) {
    std::cout << "should not get here: num = " << num << std::endl;
    assert(0);
    return async::MakeReadyPromise(std::move(num));
  })
  .Exception([](std::exception *e) {
    assert(e);
    assert(std::string(e->what()) == "NonExcep");
    // std::cout << "exception: " << e->what() << std::endl;
    return async::MakeReadyPromise();
  })
  .Then([]() {
    // std::cout << "Got EmptyReadyFuture" << std::endl;
    return async::MakeException(new TestException("TestString Exception"));
  })
  .Finally([](std::exception *e) {
    assert(e);
    if (e) {
      assert(std::string(e->what()) == "TestString Exception");
      // std::cout << "Finally exception: " << e->what() << std::endl;
      std::cout << "test succ\n";
    } else {
      std::cout << "Finally No exception" << std::endl;
      std::cout << "test fail\n";
    }
  });

  return 0;
}