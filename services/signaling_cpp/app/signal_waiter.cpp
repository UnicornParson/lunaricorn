#include "signal_waiter.h"
#include <lunaricorn.h>
SignalWaiter::SignalWaiter()
{
   sigemptyset(&set_);
   sigaddset(&set_, SIGTERM);
   sigaddset(&set_, SIGINT);
   sigaddset(&set_, SIGQUIT);

   if (pthread_sigmask(SIG_BLOCK, &set_, nullptr))
       throw std::runtime_error("signal block failed");

   MLOG_D("Signal waiter initialized");
}

int SignalWaiter::wait()
{
   int sig{};
   if (sigwait(&set_, &sig))
       throw std::runtime_error("sigwait failed");

   stopped_ = true;
   MLOG_W("Shutdown signal received: {}", sig);

   return sig;
}

