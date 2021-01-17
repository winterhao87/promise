#ifndef ASYNC_PROMISE_H_
#define ASYNC_PROMISE_H_

#include <tuple>
#include <functional>
#include <exception>
#include <memory>
#include <assert.h>

using SException = std::shared_ptr<std::exception>;

namespace async {
template <typename... T>
class Promise;

enum class status {
  pending,
  fulfilled,
  rejected
};

namespace internal {
template<typename... T>
class Runnable {
public:
  Runnable() = default;
  virtual ~Runnable() = default;

  virtual void SetException(SException e) = 0;
  virtual void SetValue(std::tuple<T...> &&value) = 0;
};

template<typename Func, typename... T>
class Resolver : public Runnable<T...> {
public:
  using BaseClass = Runnable<T...>;

  Resolver(Func &&func) : BaseClass(), _func(std::move(func)) {}
  ~Resolver() = default;

  virtual void SetException(SException e) override { assert(0); }
  virtual void SetValue(std::tuple<T...> &&value) override {
    std::apply<Func, std::tuple<T...>>(std::move(_func), std::move(value));
  }

private:
  Func _func;
};

template <typename Func, typename... T>
class Rejecter : public Runnable<T...> {
public:
  using BaseClass = Runnable<T...>;

  Rejecter(Func &&func) : BaseClass(), _func(std::move(func)) {}
  ~Rejecter() = default;

  virtual void SetException(SException e) override { _func(e); }
  virtual void SetValue(std::tuple<T...> &&value) override { assert(0); }

private:
  Func _func;
};

template <typename... T>
class State : public std::enable_shared_from_this<State<T...>> {
public:
  State() = default;
  ~State() { Reset(); }

  std::tuple<T...> &&get_value() {
    _st = status::pending;
    return std::move(_value);
  }

  SException get_exception() {
    _st = status::pending;
    return std::move(_e);
  }

  void SetValue(std::tuple<T...> &&value) {
    _st = status::fulfilled;
    _value = std::move(value);
    if (_done) {
      _done->SetValue(get_value());
    }
  }

  void SetException(SException e) {
    _st = status::rejected;
    _e = e;
    if (_fail) {
      _fail->SetException(get_exception());
    }
  }

  void SetDone(Runnable<T...> *done) {
    _done = done;
    if (_st == status::fulfilled) {
      _done->SetValue(get_value());
    }
  }

  void SetFail(Runnable<T...> *fail) {
    _fail = fail;
    if (_st == status::rejected) {
      _fail->SetException(get_exception());
    }
  }

  void SetFunc(Runnable<T...> *done, Runnable<T...> *fail) {
    _done = done;
    _fail = fail;
    switch (_st) {
    case status::fulfilled:
      return _done->SetValue(get_value());
    case status::rejected:
      return _fail->SetException(get_exception());
    default: ;
    }
  }

  void Reset() {
    delete _done;
    delete _fail;

    _st = status::pending;
    _done = nullptr;
    _fail = nullptr;
    _e = nullptr;
  }

  status _st{status::pending};
  Runnable<T...> *_done{nullptr};
  Runnable<T...> *_fail{nullptr};
  std::tuple<T...> _value;
  SException _e{nullptr};
};

template<typename... T>
class Deffered {
public:
  Deffered() { _state = std::make_shared<State<T...>>(); }
  ~Deffered() = default;

  Promise<T...> GetPromise() { return Promise<T...>(_state); }
  void SetValue(T &&... value) { _state->SetValue(std::make_tuple<T...>(std::move(value)...)); }
  void SetValue(std::tuple<T...> &&value) { _state->SetValue(std::move(value));}
  void SetException(SException e) { _state->SetException(e); }
  void Reset() { _state->Reset(); }

private:
  std::shared_ptr<State<T...>> _state;
};

} // namespace internal

template <typename... T>
class Promise {
public:
  using ValueType = std::tuple<T...>;
  using Type = Promise<T...>;
  using DefferedType = internal::Deffered<T...>;

  using ResolverType = internal::Resolver<std::function<void(T...)>, T...>;
  using RejecterType = internal::Rejecter<std::function<void(SException)>, T...>;

  Promise() { _state = std::make_shared<internal::State<T...>>(); }
  explicit Promise(std::shared_ptr<internal::State<T...>> state) : _state(state) {}
  ~Promise() = default;

  status GetStatus() { return _state->_st; }
  ValueType GetValue() { return _state->get_value(); }
  SException GetException() { return _state->get_exception(); }

  template <typename Func, typename R = typename std::result_of<Func(T &&...)>::type>
  R Then(Func &&func) {
    typename R::DefferedType $;
    auto done = [func, $](T &&... args) mutable {
      auto r = func(std::move(args)...);
      switch (r.GetStatus()) {
      case status::fulfilled:
        return $.SetValue(r.GetValue());
      case status::rejected:
        return $.SetException(r.GetException());
      default: ;
      }
    };

    auto fail = [$](SException e) mutable { $.SetException(e); };
    _state->SetFunc(new ResolverType{std::move(done)}, new RejecterType{std::move(fail)});

    return $.GetPromise();
  }

  template<typename Func, typename R = typename std::result_of<Func(SException)>::type>
  R Exception(Func &&func) {
    typename R::DefferedType $;
    auto fail = [func, $](SException e) mutable {
      auto r = func(e);
      switch (r.GetStatus()) {
      case status::fulfilled:
        return $.SetValue(r.GetValue());
      case status::rejected:
        return $.SetException(r.GetException());
      default: ;
      }
    };

    _state->SetFail(new RejecterType{std::move(fail)});
    return $.GetPromise();
  }

  void Finally(std::function<void(SException)> &&func) {
    auto done = [func](T &&... args) { func(nullptr); };
    auto fail = std::move(func);
    _state->SetFunc(new ResolverType{std::move(done)}, new RejecterType{std::move(fail)});
  }

private:
  std::shared_ptr<internal::State<T...>> _state;
};


template <typename... T>
Promise<T...> MakeReadyPromise(T &&... values) {
  internal::Deffered<T...> $;
  $.SetValue(std::move(values)...);
  return $.GetPromise();
}

template <typename... T>
Promise<T...> MakeException(std::exception *e) {
  internal::Deffered<T...> $;
  SException p(e);
  $.SetException(p);
  return $.GetPromise();
}

template <typename... T>
Promise<T...> MakeException(SException e) {
  internal::Deffered<T...> $;
  $.SetException(e);
  return $.GetPromise();
}

} // namespace async

#endif // ASYNC_PROMISE_H_ 