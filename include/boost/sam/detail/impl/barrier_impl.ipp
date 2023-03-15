// Copyright (c) 2022 Klemens D. Morgenstern
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#ifndef BOOST_SAM_IMPL_BARRIER_IMPL_IPP
#define BOOST_SAM_IMPL_BARRIER_IMPL_IPP

#include <boost/sam/detail/config.hpp>
#include <boost/sam/detail/basic_op.hpp>
#include <boost/sam/basic_barrier.hpp>
#include <condition_variable>

BOOST_SAM_BEGIN_NAMESPACE
namespace detail
{

bool barrier_impl::try_arrive()
{
  auto _ = internal_lock();
  if (--counter_ == 0u)
  {
    waiters_.complete_all({});
    counter_ = init_;
    return true;
  }
  else
    counter_++;
  return false;
}

void barrier_impl::arrive(error_code &ec)
{
    if (try_arrive())
        return;
    else if (!this->mtx_.enabled())
    {
      BOOST_SAM_ASSIGN_EC(ec, asio::error::in_progress);
      return ;
    }

    struct op_t final : detail::wait_op
    {
        error_code &ec;
        std::atomic<bool> done = false;
        detail::event var;
        lock_type &lock;
        op_t(error_code & ec, lock_type & lock) : ec(ec), lock(lock) {}

        void complete(error_code ec) override
        {
          done = true;
          this->ec = ec;
          var.signal_all(lock);
          this->unlink();
        }

        void shutdown() override
        {
          done = true;
          BOOST_SAM_ASSIGN_EC(this->ec, net::error::shut_down);
          var.signal_all(lock);
          this->unlink();
        }

        void wait()
        {
          while (!done)
            var.wait(lock);
        }
    };
    auto lock = this->internal_lock();
    decrement();

    // double check!
    if (counter_ == 0u)
    {
      waiters_.complete_all({});
      counter_ = init_;
      return ;
    }
    op_t op{ec, lock};
    add_waiter(&op);
    op.wait();
}

void
barrier_impl::add_waiter(detail::wait_op *waiter) noexcept
{
    waiter->link_before(&waiters_);
}


}
BOOST_SAM_END_NAMESPACE


#endif //BOOST_SAM_IMPL_BARRIER_IMPL_IPP