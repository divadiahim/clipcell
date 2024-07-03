#include "fdm.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <tllist.h>
#include <unistd.h>

struct fd_handler {
   int fd;
   int events;
   fdm_fd_handler_t callback;
   void *callback_data;
   bool deleted;
};

struct sig_handler {
   fdm_signal_handler_t callback;
   void *callback_data;
};

struct fdm {
   int epoll_fd;
   bool is_polling;
   tll(struct fd_handler *) fds;
   tll(struct fd_handler *) deferred_delete;

   sigset_t sigmask;
   struct sig_handler *signal_handlers;
};

static volatile sig_atomic_t got_signal = false;
static volatile sig_atomic_t *received_signals = NULL;

struct fdm *
fdm_init(void) {
   sigset_t sigmask;
   if (sigprocmask(0, NULL, &sigmask) < 0) {
      LOG(10, "failed to get process signal mask");
      return NULL;
   }

   int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
   if (epoll_fd == -1) {
      LOG(10, "failed to create epoll FD");
      return NULL;
   }

   assert(received_signals == NULL); /* Only one FDM instance supported */
   received_signals = calloc(SIGRTMAX, sizeof(received_signals[0]));
   got_signal = false;

   struct fdm *fdm = malloc(sizeof(*fdm));
   if (unlikely(fdm == NULL)) {
      LOG(10, "malloc() failed");
      return NULL;
   }

   struct sig_handler *sig_handlers = calloc(SIGRTMAX, sizeof(sig_handlers[0]));

   if (sig_handlers == NULL) {
      LOG(10, "failed to allocate signal handler array");
      free(fdm);
      return NULL;
   }

   *fdm = (struct fdm){
       .epoll_fd = epoll_fd,
       .is_polling = false,
       .fds = tll_init(),
       .deferred_delete = tll_init(),
       .sigmask = sigmask,
       .signal_handlers = sig_handlers,
   };
   return fdm;
}

void fdm_destroy(struct fdm *fdm) {
   if (fdm == NULL)
      return;

   if (tll_length(fdm->fds) > 0)
      LOG(10, "FD list not empty");

   for (int i = 0; i < SIGRTMAX; i++) {
      if (fdm->signal_handlers[i].callback != NULL)
         LOG(10, "handler for signal %d not removed", i);
   }
   assert(tll_length(fdm->fds) == 0);
   assert(tll_length(fdm->deferred_delete) == 0);

   sigprocmask(SIG_SETMASK, &fdm->sigmask, NULL);
   free(fdm->signal_handlers);

   tll_free(fdm->fds);
   tll_free(fdm->deferred_delete);
   close(fdm->epoll_fd);
   free(fdm);

   free((void *)received_signals);
   received_signals = NULL;
}

bool fdm_add(struct fdm *fdm, int fd, int events, fdm_fd_handler_t cb, void *data) {
   struct fd_handler *handler = malloc(sizeof(*handler));
   if (unlikely(handler == NULL)) {
      LOG(10, "malloc() failed");
      return false;
   }

   *handler = (struct fd_handler){
       .fd = fd,
       .events = events,
       .callback = cb,
       .callback_data = data,
       .deleted = false,
   };

   tll_push_back(fdm->fds, handler);

   struct epoll_event ev = {
       .events = events,
       .data = {.ptr = handler},
   };

   if (epoll_ctl(fdm->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
      LOG(10, "failed to register FD=%d with epoll", fd);
      free(handler);
      tll_pop_back(fdm->fds);
      return false;
   }

   return true;
}

static bool
fdm_del_internal(struct fdm *fdm, int fd, bool close_fd) {
   if (fd == -1)
      return true;

   tll_foreach(fdm->fds, it) {
      if (it->item->fd != fd)
         continue;

      if (epoll_ctl(fdm->epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0)
         LOG(10, "failed to unregister FD=%d from epoll", fd);

         if (close_fd)
            close(it->item->fd);

      it->item->deleted = true;
      if (fdm->is_polling)
         tll_push_back(fdm->deferred_delete, it->item);
      else
         free(it->item);

      tll_remove(fdm->fds, it);
      return true;
   }

   LOG(10, "no such FD: %d", fd);
   close(fd);
   return false;
}

bool fdm_del(struct fdm *fdm, int fd) {
   return fdm_del_internal(fdm, fd, true);
}

bool fdm_del_no_close(struct fdm *fdm, int fd) {
   return fdm_del_internal(fdm, fd, false);
}

static bool
event_modify(struct fdm *fdm, struct fd_handler *fd, int new_events) {
   if (new_events == fd->events)
      return true;

   struct epoll_event ev = {
       .events = new_events,
       .data = {.ptr = fd},
   };

   if (epoll_ctl(fdm->epoll_fd, EPOLL_CTL_MOD, fd->fd, &ev) < 0) {
      LOG(10, "failed to modify FD=%d with epoll (events 0x%08x -> 0x%08x)",
         fd->fd, fd->events, new_events);
      return false;
   }

   fd->events = new_events;
   return true;
}

bool fdm_event_add(struct fdm *fdm, int fd, int events) {
   tll_foreach(fdm->fds, it) {
      if (it->item->fd != fd)
         continue;

      return event_modify(fdm, it->item, it->item->events | events);
   }

   LOG(10, "FD=%d not registered with the FDM", fd);
   return false;
}

bool fdm_event_del(struct fdm *fdm, int fd, int events) {
   tll_foreach(fdm->fds, it) {
      if (it->item->fd != fd)
         continue;

      return event_modify(fdm, it->item, it->item->events & ~events);
   }

   LOG(10, "FD=%d not registered with the FDM", fd);
   return false;
}

static void
signal_handler(int signo) {
   got_signal = true;
   received_signals[signo] = true;
}

bool fdm_signal_add(struct fdm *fdm, int signo, fdm_signal_handler_t handler, void *data) {
   if (fdm->signal_handlers[signo].callback != NULL) {
      LOG(10, "signal %d already has a handler", signo);
      return false;
   }

   sigset_t mask, original;
   sigemptyset(&mask);
   sigaddset(&mask, signo);

   if (sigprocmask(SIG_BLOCK, &mask, &original) < 0) {
       LOG(10, "failed to block signal %d", signo);
      return false;
   }

   struct sigaction action = {.sa_handler = &signal_handler};
   sigemptyset(&action.sa_mask);
   if (sigaction(signo, &action, NULL) < 0) {
      LOG(10, "failed to set signal handler for signal %d", signo);
      sigprocmask(SIG_SETMASK, &original, NULL);
      return false;
   }

   received_signals[signo] = false;
   fdm->signal_handlers[signo].callback = handler;
   fdm->signal_handlers[signo].callback_data = data;
   return true;
}

bool fdm_signal_del(struct fdm *fdm, int signo) {
   if (fdm->signal_handlers[signo].callback == NULL)
      return false;

   struct sigaction action = {.sa_handler = SIG_DFL};
   sigemptyset(&action.sa_mask);
   if (sigaction(signo, &action, NULL) < 0) {
      LOG(10, "failed to restore signal handler for signal %d", signo);
      return false;
   }

   received_signals[signo] = false;
   fdm->signal_handlers[signo].callback = NULL;
   fdm->signal_handlers[signo].callback_data = NULL;

   sigset_t mask;
   sigemptyset(&mask);
   sigaddset(&mask, signo);
   if (sigprocmask(SIG_UNBLOCK, &mask, NULL) < 0) {
      LOG(10, "failed to unblock signal %d", signo);
      return false;
   }

   return true;
}

bool fdm_poll(struct fdm *fdm) {
   assert(!fdm->is_polling && "nested calls to fdm_poll() not allowed");
   if (fdm->is_polling) {
      LOG(10, "nested calls to fdm_poll() not allowed");
      return false;
   }

   struct epoll_event events[tll_length(fdm->fds)];

   int r = epoll_pwait(
       fdm->epoll_fd, events, tll_length(fdm->fds), -1, &fdm->sigmask);

   int errno_copy = errno;

   if (unlikely(got_signal)) {
      got_signal = false;

      for (int i = 0; i < SIGRTMAX; i++) {
         if (received_signals[i]) {
            received_signals[i] = false;
            struct sig_handler *handler = &fdm->signal_handlers[i];

            assert(handler->callback != NULL);
            if (!handler->callback(fdm, i, handler->callback_data))
               return false;
         }
      }
   }

   if (unlikely(r < 0)) {
      if (errno_copy == EINTR)
         return true;

      LOG(10, "failed to epoll");
      return false;
   }

   bool ret = true;

   fdm->is_polling = true;
   for (int i = 0; i < r; i++) {
      struct fd_handler *fd = events[i].data.ptr;
      if (fd->deleted)
         continue;

      if (!fd->callback(fdm, fd->fd, events[i].events, fd->callback_data)) {
         ret = false;
         break;
      }
   }
   fdm->is_polling = false;

   tll_foreach(fdm->deferred_delete, it) {
      free(it->item);
      tll_remove(fdm->deferred_delete, it);
   }

   return ret;
}
