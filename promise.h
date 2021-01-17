#ifndef PROMISE_H_
#define PROMISE_H_

#include <string>
#include <tuple>
#include <functional>
#include <exception>
#include <atomic>
#include <memory>

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
  ~Runnable() = default;

  void SetException(std::exception *e) { _e = e; }
  std::exception* GetException() const { return _e; }

  void SetValue(std::tuple<T...> &&value) { _value = std::move(value); }
  std::tuple<T...> GetValue() const { return _value; }

  virtual void Run() = 0;

protected:
  std::tuple<T...> _value;
  std::exception *_e;
};

template<typename Func, typename... T>
class Resolver : public Runnable<T...> {
public:
  using BaseClass = Runnable<T...>;

  Resolver(Func &&func) : BaseClass(), _func(std::move(func)) {}
  ~Resolver() = default;

  virtual void Run() override { std::apply<Func, std::tuple<T...>>(std::move(_func), std::move(BaseClass::GetValue())); }

private:
  Func _func;
};

template <typename Func, typename... T>
class Rejecter : public Runnable<T...> {
public:
  using BaseClass = Runnable<T...>;

  Rejecter(Func &&func) : BaseClass(), _func(std::move(func)) {}
  ~Rejecter() = default;

  virtual void Run() override { _func(BaseClass::GetException()); }

private:
  Func _func;
};

template <typename... T>
class State : public std::enable_shared_from_this<State<T...>> {
public:
  State() = default;
  ~State() = default;

  void SetValue(std::tuple<T...> &&value) {
    _st = status::fulfilled;
    _value = std::move(value);
    if (_done) {
      _done->SetValue(std::move(_value));
      _done->Run();
    }
  }

  void SetException(std::exception *e) {
    _st = status::rejected;
    _e = std::move(e);
    if (_fail) {
      _fail->SetException(std::move(_e));
      _fail->Run();
    }
  }

  void SetDone(Runnable<T...> *done) {
    _done = done;
    if (_st == status::fulfilled) {
      _done->SetValue(std::move(_value));
      _done->Run();
    }
  }

  void SetFail(Runnable<T...> *fail) {
    _fail = fail;
    if (_st == status::rejected) {
      _fail->SetException(std::move(_e));
      _fail->Run();
    }
  }

  void SetFunc(Runnable<T...> *done, Runnable<T...> *fail) {
    _done = done;
    _fail = fail;
    switch (_st) {
    case status::fulfilled:
      _done->SetValue(std::move(_value));
      return _done->Run();
    case status::rejected:
      _fail->SetException(std::move(_e));
      return _fail->Run();
    default: ;
    }
  }

  void Reset() {
    _st = status::pending;
    _done = nullptr;
    _fail = nullptr;
    _e = nullptr;
  }

  status _st{status::pending};
  Runnable<T...> *_done{nullptr};
  Runnable<T...> *_fail{nullptr};
  std::tuple<T...> _value;
  std::exception *_e{nullptr};
};

template<typename... T>
class Deffered {
public:
  Deffered() { _state = std::make_shared<State<T...>>(); }
  ~Deffered() = default;

  Promise<T...> GetPromise() { return Promise<T...>(_state); }
  void SetValue(T &&... value) { _state->SetValue(std::make_tuple<T...>(std::move(value)...)); }
  void SetValue(std::tuple<T...> &&value) { _state->SetValue(std::move(value));}
  void SetException(std::exception *e) { _state->SetException(std::move(e)); }

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
  using RejecterType = internal::Rejecter<std::function<void(std::exception *)>, T...>;

  Promise() { _state = std::make_shared<internal::State<T...>>(); }
  explicit Promise(std::shared_ptr<internal::State<T...>> state) : _state(state) {}
  ~Promise() = default;

  status GetStatus() { return _state->_st; }
  ValueType GetValue() { return _state->_value; }
  std::exception* GetException() { return _state->_e; }

  template <typename Func, typename R = typename std::result_of<Func(T &&...)>::type>
  R Then(Func &&func) {
    typename R::DefferedType $;
    auto done = [func, $](T &&... args) mutable {
      auto r = func(std::move(args)...);
      switch (r.GetStatus()) {
      case status::fulfilled:
        return $.SetValue(std::move(r.GetValue()));
      case status::rejected:
        return $.SetException(r.GetException());
      default: ;
      }
    };

    auto fail = [$](std::exception *e) mutable { $.SetException(e); };
    _state->SetFunc(new ResolverType{std::move(done)}, new RejecterType{std::move(fail)});

    return $.GetPromise();
  }

  template<typename Func, typename R = typename std::result_of<Func(std::exception*)>::type>
  R Exception(Func &&func) {
    typename R::DefferedType $;
    auto fail = [func, $](std::exception *e) mutable {
      auto r = func(std::move(e));
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

  void Finally(std::function<void(std::exception*)> &&func) {
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
  $.SetException(std::move(e));
  return $.GetPromise();
}

} // namespace async

#endif // PROMISE_H_ 