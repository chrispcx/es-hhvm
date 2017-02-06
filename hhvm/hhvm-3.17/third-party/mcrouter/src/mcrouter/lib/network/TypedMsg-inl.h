/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

namespace facebook { namespace memcache { namespace detail {

template <class Proc, class... Args>
using DispatchFunc = void (*)(Proc&, Args...);

 /* Function pointer for Proc::processMsg<M> */
template <class M, class Proc, class... Args>
struct DispatchImpl {
  static constexpr DispatchFunc<Proc, Args...> func =
      &Proc::template processMsg<M>;
};

/* If M is void, use nullptr function pointer */
template <class Proc, class... Args>
struct DispatchImpl<void, Proc, Args...> {
  static constexpr DispatchFunc<Proc, Args...> func = nullptr;
};

template <class Message, size_t MaxId, class Proc, class... Args>
struct CallDispatcherImplExpanded;

/* Contains a single array that maps Ids to processMsg calls */
template <class... Ms, size_t MaxId, class Proc, class... Args>
struct CallDispatcherImplExpanded<List<Ms...>, MaxId, Proc, Args...> {
  static constexpr DispatchFunc<Proc, Args...> array_[MaxId + 1] = {
      DispatchImpl<Ms, Proc, Args...>::func...};
};

/* Array needs definition outside of the class */
template <class... Ms, size_t MaxId, class Proc, class... Args>
constexpr DispatchFunc<Proc, Args...>
    CallDispatcherImplExpanded<List<Ms...>, MaxId, Proc, Args...>::array_
        [MaxId + 1];

// Sort List<Ms...> by M::typeId, expand to fill 0s, call ImplExpanded
template <class... Ms, class Proc, class... Args>
struct CallDispatcherImpl<List<Ms...>, Proc, Args...>
    : public CallDispatcherImplExpanded<
          ExpandT<SortT<List<Ms...>>>,
          Max<Ms::typeId...>::value,
          Proc,
          Args...> {};

template <class T, class PairList>
struct RequestFromReplyTypeImpl;

template <class T>
struct RequestFromReplyTypeImpl<T, List<>> {
  using type = void;
};

template <class T, class P, class... Ps>
struct RequestFromReplyTypeImpl<T, List<P, Ps...>> {
  using type = typename std::conditional<
      std::is_same<T, typename P::Second>::value,
      typename P::First,
      typename RequestFromReplyTypeImpl<T, List<Ps...>>::type>::type;
};

}}}  // facebook::memcache::detail
