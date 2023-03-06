// Copyright (c) 2022 Richard Hodges
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Disable autolinking for unit tests.
#if !defined(BOOST_ALL_NO_LIB)
#define BOOST_ALL_NO_LIB 1
#endif // !defined(BOOST_ALL_NO_LIB)

// Test that header file is self-contained.

#include <boost/test/unit_test.hpp>

#include <boost/asem/condition_variable.hpp>
#include <boost/optional.hpp>
#include <chrono>
#include <random>


#if defined(BOOST_ASEM_STANDALONE)

#include <asio.hpp>
#include <asio/coroutine.hpp>
#include <asio/detached.hpp>
#include <asio/yield.hpp>
#include <asio/append.hpp>
#include <asio/as_tuple.hpp>
#include <asio/experimental/parallel_group.hpp>

namespace net = ::asio;

#else

namespace asio = boost::asio;
#include <boost/asio.hpp>
#include <boost/asio/coroutine.hpp>
#include <boost/asio/yield.hpp>
#include <boost/asio/append.hpp>
#include <boost/asio/experimental/as_tuple.hpp>
#include <boost/asio/experimental/parallel_group.hpp>

namespace net = boost::asio;

#endif

using namespace std::literals;
using namespace BOOST_ASEM_NAMESPACE;
using namespace net;

using models = std::tuple<net::io_context, net::thread_pool>;

inline void run_impl(io_context & ctx)
{
    ctx.run();
}

inline void run_impl(thread_pool & ctx)
{
    ctx.join();
}

BOOST_AUTO_TEST_SUITE(basic_condition_variable_test)

BOOST_AUTO_TEST_CASE_TEMPLATE(cancel_all, T, models)
{
    T ioc{};
    auto l =
            [&]
            {
                auto cv  = condition_variable(ioc.get_executor());
                cv.async_wait([](error_code ec){BOOST_CHECK_EQUAL(ec, asio::error::operation_aborted);});
                cv.async_wait([](error_code ec){BOOST_CHECK_EQUAL(ec, asio::error::operation_aborted);});
                cv.async_wait([](error_code ec){BOOST_CHECK_EQUAL(ec, asio::error::operation_aborted);});
                cv.async_wait([](error_code ec){BOOST_CHECK_EQUAL(ec, asio::error::operation_aborted);});
            };
    post(ioc, l);
    run_impl(ioc);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(notify_all, T, models)
{
    T ioc{};
    auto cv  = condition_variable(ioc.get_executor());
    int cnt = 0;
    cv.async_wait([&](error_code ec){cnt |= 1; BOOST_CHECK(!ec);});
    cv.async_wait([&](error_code ec){cnt |= 2; BOOST_CHECK(!ec);});
    cv.async_wait([&](error_code ec){cnt |= 4; BOOST_CHECK(!ec);});
    cv.async_wait([&](error_code ec){cnt |= 8; BOOST_CHECK(!ec);});
    post(ioc, [&]{cv.notify_all();});
    run_impl(ioc);

    BOOST_CHECK_EQUAL(cnt, 15);
}


BOOST_AUTO_TEST_CASE_TEMPLATE(notify_one, T, models)
{
    T ioc{};
    boost::optional<condition_variable> store{ioc.get_executor()};
    auto & cv = *store;
    int cnt = 0;
    cv.async_wait([&](error_code ec){if (!ec) cnt |= 1; BOOST_CHECK(!ec);});
    cv.async_wait([&](error_code ec){if (!ec) cnt |= 2; BOOST_CHECK(!ec);});
    cv.async_wait([&](error_code ec){if (!ec) cnt |= 4; BOOST_CHECK_EQUAL(ec, asio::error::operation_aborted);});
    cv.async_wait([&](error_code ec){if (!ec) cnt |= 8; BOOST_CHECK_EQUAL(ec, asio::error::operation_aborted);});
    cv.notify_one();
    cv.notify_one();

    asio::steady_timer tim{ioc, std::chrono::milliseconds{100}};
    tim.async_wait([&](error_code ec){store.reset();});

    run_impl(ioc);
    BOOST_CHECK_EQUAL(cnt, 3);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(notify_some, T, models)
{
    T ioc{};
    auto store  = boost::optional<condition_variable>(ioc.get_executor());
    auto & cv = *store;

    int cnt = 0;
    cv.async_wait([]{return false;}, [&](error_code ec){if (!ec) cnt |= 1; BOOST_CHECK_EQUAL(ec, asio::error::operation_aborted);});
    cv.async_wait([]{return true;},  [&](error_code ec){if (!ec) cnt |= 2; BOOST_CHECK(!ec);});
    cv.async_wait([]{return false;}, [&](error_code ec){if (!ec) cnt |= 4; BOOST_CHECK_EQUAL(ec, asio::error::operation_aborted);});
    cv.async_wait([]{return true;},  [&](error_code ec){if (!ec) cnt |= 8; BOOST_CHECK(!ec);});

    cv.notify_all();

    asio::steady_timer tim{ioc, std::chrono::milliseconds{100}};
    tim.async_wait([&](error_code ec){store.reset();});

    run_impl(ioc);

    BOOST_CHECK_EQUAL(cnt, 10);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(notify_some_more, T, models)
{
    T ioc{};
    auto store  = boost::optional<condition_variable>(ioc.get_executor());
    auto & cv = *store;

    int cnt = 0;
    cv.async_wait([&](error_code ec){if (!ec) cnt |= 1; BOOST_CHECK(!ec);});
    cv.async_wait([&](error_code ec){if (!ec) cnt |= 2; BOOST_CHECK(!ec);});
    cv.async_wait([&](error_code ec){if (!ec) cnt |= 4; BOOST_CHECK(!ec);});
    cv.async_wait([&](error_code ec){if (!ec) cnt |= 8; BOOST_CHECK_EQUAL(ec, asio::error::operation_aborted); });

    cv.notify_one();
    cv.notify_one();
    cv.notify_one();

    asio::steady_timer tim{ioc, std::chrono::milliseconds{100}};
    tim.async_wait([&](error_code ec){store.reset();});

    run_impl(ioc);
    BOOST_CHECK_EQUAL(cnt, 7);
}


BOOST_AUTO_TEST_CASE(rebind_condition_variable)
{
    net::io_context ctx;
    auto res = net::deferred.as_default_on(condition_variable{ctx.get_executor()});

    res = condition_variable{ctx.get_executor()};
}

BOOST_AUTO_TEST_CASE_TEMPLATE(shutdown_, T, models)
{
  io_context ctx;
  auto smtx = std::make_shared<condition_variable>(ctx);

  auto l =  [smtx](error_code ec) { BOOST_CHECK(false); };

  smtx->async_wait(l);
}

BOOST_AUTO_TEST_SUITE_END()