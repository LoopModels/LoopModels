#pragma once

template <typename T>
concept OStream = requires(T &os) {
  { os << "hello world" } -> std::convertible_to<T &>;
};
