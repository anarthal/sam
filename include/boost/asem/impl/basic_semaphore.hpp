//
// Copyright (c) 2021 Richard Hodges (hodges.r@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/madmongo1/asio_experiments
//

#ifndef BOOST_ASEM_IMPL_BASIC_SEMAPHORE_HPP
#define BOOST_ASEM_IMPL_BASIC_SEMAPHORE_HPP

#include <boost/asem/basic_semaphore.hpp>
#include <boost/asem/detail/basic_op_model.hpp>

#if defined(BOOST_ASEM_STANDALONE)
#include <asio/post.hpp>
#else
#include <boost/asio/post.hpp>
#endif


BOOST_ASEM_BEGIN_NAMESPACE

template < class Executor >
basic_semaphore< Executor >::basic_semaphore(executor_type exec, int initial_count)
: exec_(std::move(exec))
, impl_(net::query(exec_, net::execution::context), initial_count)
{
}

template < class Executor >
auto
basic_semaphore< Executor >::get_executor() const noexcept -> executor_type
{
    return exec_;
}


template < class Executor >
struct basic_semaphore<Executor>::async_aquire_op
{
    basic_semaphore<Executor> * self;

    template< class Handler >
    void operator()(Handler &&handler)
    {
        auto e = get_associated_executor(handler, self->get_executor());
        auto l = self->impl_.internal_lock();
        ignore_unused(l);
        if (self->impl_.count() > 0)
        {
            self->impl_.decrement();
            auto ie = net::get_associated_immediate_executor(handler, self->get_executor());
            net::post(ie, net::append(std::forward< Handler >(handler), error_code()));
            return ;
        }

        using handler_type = std::decay_t< Handler >;
        using model_type = detail::basic_op_model< decltype(e), handler_type, void(error_code)>;
        model_type *model = model_type ::construct(std::move(e), std::forward< Handler >(handler));
        auto slot = model->get_cancellation_slot();
        if (slot.is_connected())
        {
            auto &impl = self->impl_;
            slot.assign(
                [model, &impl, slot](net::cancellation_type type)
                {

                    if (type != net::cancellation_type::none)
                    {
                      auto sl = slot;
                      auto lock = impl.internal_lock();
                      ignore_unused(lock);
                      // completed already
                      if (!sl.is_connected())
                        return;

                      auto *self = model;
                      self->complete(net::error::operation_aborted);
                    }
                });
        }
        self->impl_.add_waiter(model);
    }
};

template < class Executor >
template < BOOST_ASEM_COMPLETION_TOKEN_FOR(void(error_code)) CompletionHandler >
BOOST_ASEM_INITFN_AUTO_RESULT_TYPE(CompletionHandler, void(error_code))
basic_semaphore< Executor >::async_acquire(CompletionHandler &&token)
{
    return net::async_initiate< CompletionHandler, void(std::error_code) >(
                async_aquire_op{this}, token);
}

BOOST_ASEM_END_NAMESPACE

#endif
