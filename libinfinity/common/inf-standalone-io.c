/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-io.h>

/* TODO: Implement InfStandaloneIo on Win32, probably using WSAEventSelect.
 * The implementations should be in separate files, with this functions just
 * being forwarders. */

#ifndef G_OS_WIN32

#include <poll.h>
#include <errno.h>
#include <string.h>

#endif /* !G_OS_WIN32 */

#ifndef G_OS_WIN32

typedef struct _InfStandaloneIoWatch InfStandaloneIoWatch;
struct _InfStandaloneIoWatch {
  struct pollfd* pfd;
  InfNativeSocket* socket;
  InfIoFunc func;
  gpointer user_data;
  GDestroyNotify notify;
};

typedef struct _InfStandaloneIoTimeout InfStandaloneIoTimeout;
struct _InfStandaloneIoTimeout {
  GTimeVal begin;
  guint msecs;
  InfIoTimeoutFunc func;
  gpointer user_data;
  GDestroyNotify notify;
};

#endif /* !G_OS_WIN32 */

typedef struct _InfStandaloneIoPrivate InfStandaloneIoPrivate;
struct _InfStandaloneIoPrivate {
#ifndef G_OS_WIN32
  struct pollfd* pfds;
  InfStandaloneIoWatch* watches;

  guint fd_size;
  guint fd_alloc;

  GList* timeouts;

  gboolean loop_running;
#endif /* !G_OS_WIN32 */
};

#define INF_STANDALONE_IO_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_STANDALONE_IO, InfStandaloneIoPrivate))

static GObjectClass* parent_class;

#ifndef G_OS_WIN32

static guint
inf_standalone_io_timeval_diff(GTimeVal* first,
                               GTimeVal* second)
{
  g_assert(first->tv_sec > second->tv_sec ||
           (first->tv_sec == second->tv_sec &&
            first->tv_usec >= second->tv_usec));

  /* Don't risk overflow, don't need to convert to signed int */
  return (first->tv_sec - second->tv_sec) * 1000 +
         (first->tv_usec+500)/1000 - (second->tv_usec+500)/1000;
}

static void
inf_standalone_io_iteration_impl(InfStandaloneIo* io,
                                 int timeout)
{
  InfStandaloneIoPrivate* priv;
  InfIoEvent events;
  int result;
  guint i;

  GList* item;
  GTimeVal current;
  InfStandaloneIoTimeout* cur_timeout;
  GList* next_timeout;
  guint elapsed;

  priv = INF_STANDALONE_IO_PRIVATE(io);

  g_get_current_time(&current);
  next_timeout = NULL;

  for(item = priv->timeouts; item != NULL; item = g_list_next(item))
  {
    cur_timeout = item->data;
    elapsed = inf_standalone_io_timeval_diff(&current, &cur_timeout->begin);

    if(elapsed >= cur_timeout->msecs)
    {
      /* already elapsed */
      /* TODO: Don't even poll */
      timeout = 0;
      next_timeout = item;

      /* no need to check other timeouts */
      break;
    }
    else
    {
      if(timeout < 0 ||cur_timeout->msecs - elapsed > (guint)timeout)
      {
        next_timeout = item;
        timeout = cur_timeout->msecs - elapsed;
      }
    }
  }

  result = poll(priv->pfds, (nfds_t)priv->fd_size, timeout);

  if(result == -1)
  {
    if(errno != EINTR)
      g_warning("poll() failed: %s\n", strerror(errno));

    return;
  }

  g_object_ref(G_OBJECT(io));

  if(result == 0 && next_timeout != NULL)
  {
    /* No file descriptor is active, but a timeout elapsed */
    cur_timeout = next_timeout->data;
    priv->timeouts = g_list_delete_link(priv->timeouts, next_timeout);

    cur_timeout->func(cur_timeout->user_data);
    if(cur_timeout->notify)
      cur_timeout->notify(cur_timeout->user_data);
    g_slice_free(InfStandaloneIoTimeout, cur_timeout);
  }
  else if(result > 0)
  {
    while(result--)
    {
      for(i = 0; i < priv->fd_size; ++ i)
      {
        if(priv->pfds[i].revents != 0)
        {
          events = 0;
          if(priv->pfds[i].revents & POLLIN)
            events |= INF_IO_INCOMING;
          if(priv->pfds[i].revents & POLLOUT)
            events |= INF_IO_OUTGOING;
          /* We treat POLLPRI as error because it should not occur in
           * infinote. */
          if(priv->pfds[i].revents & (POLLERR | POLLPRI | POLLHUP | POLLNVAL))
            events |= INF_IO_ERROR;
          
          priv->pfds[i].revents = 0;

          priv->watches[i].func(
            priv->watches[i].socket,
            events,
            priv->watches[i].user_data
          );

          /* The callback might have done everything, including completly
           * screwing up the array of file descriptors. This is why we break
           * here and iterate from the beginning to find the next event. */
          break;
        }
      }
    }
  }

  g_object_unref(G_OBJECT(io));
}

#endif /* !G_OS_WIN32 */

static void
inf_standalone_io_init(GTypeInstance* instance,
                       gpointer g_class)
{
  InfStandaloneIo* io;
  InfStandaloneIoPrivate* priv;

  io = INF_STANDALONE_IO(instance);
  priv = INF_STANDALONE_IO_PRIVATE(io);

#ifndef G_OS_WIN32
  priv->pfds = g_malloc(sizeof(struct pollfd) * 4);
  priv->watches = g_malloc(sizeof(InfStandaloneIoWatch) * 4);

  priv->fd_size = 0;
  priv->fd_alloc = 4;

  priv->loop_running = FALSE;
#endif /* !G_OS_WIN32 */
}

static void
inf_standalone_io_finalize(GObject* object)
{
  InfStandaloneIo* io;
  InfStandaloneIoPrivate* priv;
#ifndef G_OS_WIN32
  guint i;
  GList* item;
  InfStandaloneIoTimeout* timeout;
#endif /* !G_OS_WIN32 */

  io = INF_STANDALONE_IO(object);
  priv = INF_STANDALONE_IO_PRIVATE(io);

#ifndef G_OS_WIN32
  for(i = 0; i < priv->fd_size; ++ i)
    if(priv->watches[i].notify)
      priv->watches[i].notify(priv->watches[i].user_data);

  for(item = priv->timeouts; item != NULL; item = g_list_next(item))
  {
    timeout = (InfStandaloneIoTimeout*)item->data;
    if(timeout->notify)
      timeout->notify(timeout->user_data);
  }

  g_free(priv->pfds);
  g_free(priv->watches);
  g_list_free(priv->timeouts);
#endif /* !G_OS_WIN32 */

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_standalone_io_io_watch(InfIo* io,
                           InfNativeSocket* socket,
                           InfIoEvent events,
                           InfIoFunc func,
                           gpointer user_data,
                           GDestroyNotify notify)
{
#ifndef G_OS_WIN32
  InfStandaloneIoPrivate* priv;
  int pevents;
  guint i;

  priv = INF_STANDALONE_IO_PRIVATE(io);

  pevents = 0;
  if(events & INF_IO_INCOMING)
    pevents |= POLLIN;
  if(events & INF_IO_OUTGOING)
    pevents |= POLLOUT;
  if(events & INF_IO_ERROR)
    pevents |= (POLLERR | POLLHUP | POLLNVAL | POLLPRI);

  for(i = 0; i < priv->fd_size; ++ i)
  {
    if(priv->watches[i].socket == socket)
    {
      if(events == 0)
      {
        /* Free user_data */
        if(priv->watches[i].notify)
          priv->watches[i].notify(priv->watches[i].user_data);

        /* Remove watch by replacing it by the last pollfd/watch */
        if(i != priv->fd_size - 1)
        {
          memcpy(
            &priv->pfds[i],
            &priv->pfds[priv->fd_size - 1],
            sizeof(struct pollfd)
          );

          memcpy(
            &priv->watches[i],
            &priv->watches[priv->fd_size - 1],
            sizeof(InfStandaloneIoWatch)
          );

          priv->watches[i].pfd = &priv->pfds[i];
        }

        -- priv->fd_size;
      }
      else
      {
        /* Free userdata before update */
        if(priv->watches[i].notify)
          priv->watches[i].notify(priv->watches[i].user_data);

        /* Update */
        priv->pfds[i].events = pevents;

        priv->watches[i].func = func;
        priv->watches[i].user_data = user_data;
        priv->watches[i].notify = notify;
      }

      return;
    }
  }

  /* Socket is not already present, so create new watch */
  if(priv->fd_size == priv->fd_alloc)
  {
    priv->fd_alloc += 4;

    priv->pfds = g_realloc(
      priv->pfds,
      priv->fd_alloc * sizeof(struct pollfd)
    );

    priv->watches = g_realloc(
      priv->watches,
      priv->fd_alloc * sizeof(InfStandaloneIoWatch)
    );
  }

  priv->pfds[priv->fd_size].fd = *socket;
  priv->pfds[priv->fd_size].events = pevents;
  priv->pfds[priv->fd_size].revents = 0;

  priv->watches[priv->fd_size].pfd = &priv->pfds[priv->fd_size];
  priv->watches[priv->fd_size].socket = socket;
  priv->watches[priv->fd_size].func = func;
  priv->watches[priv->fd_size].user_data = user_data;
  priv->watches[priv->fd_size].notify = notify;

  ++ priv->fd_size;
#else /* !G_OS_WIN32 */
  g_warning("InfStandaloneIo is not implemented on Win32");
#endif
}

static gpointer
inf_standalone_io_io_add_timeout(InfIo* io,
                                 guint msecs,
                                 InfIoTimeoutFunc func,
                                 gpointer user_data,
                                 GDestroyNotify notify)
{
#ifndef G_OS_WIN32
  InfStandaloneIoPrivate* priv;
  InfStandaloneIoTimeout* timeout;

  priv = INF_STANDALONE_IO_PRIVATE(io);
  timeout = g_slice_new(InfStandaloneIoTimeout);

  g_get_current_time(&timeout->begin);
  timeout->msecs = msecs;
  timeout->func = func;
  timeout->user_data = user_data;
  timeout->notify = notify;
  priv->timeouts = g_list_prepend(priv->timeouts, timeout);

  return timeout;
#else /* !G_OS_WIN32 */
  g_warning("InfStandaloneIo is not implemented on Win32");
  return NULL;
#endif
}

static void
inf_standalone_io_io_remove_timeout(InfIo* io,
                                    gpointer timeout_handle)
{
#ifndef G_OS_WIN32
  InfStandaloneIoPrivate* priv;
  InfStandaloneIoTimeout* timeout;
  GList* item;

  priv = INF_STANDALONE_IO_PRIVATE(io);
  item = g_list_find(priv->timeouts, timeout_handle);
  g_assert(item != NULL);

  timeout = item->data;
  priv->timeouts = g_list_delete_link(priv->timeouts, item);

  if(timeout->notify)
    timeout->notify(timeout->user_data);

  g_slice_free(InfStandaloneIoTimeout, timeout);
#else /* !G_OS_WIN32 */
  g_warning("InfStandaloneIo is not implemented on Win32");
#endif
}

static void
inf_standalone_io_class_init(gpointer g_class,
                             gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfStandaloneIoPrivate));

  object_class->finalize = inf_standalone_io_finalize;
}

static void
inf_standalone_io_io_init(gpointer g_iface,
                          gpointer iface_data)
{
  InfIoIface* iface;
  iface = (InfIoIface*)g_iface;

  iface->watch = inf_standalone_io_io_watch;
  iface->add_timeout = inf_standalone_io_io_add_timeout;
  iface->remove_timeout = inf_standalone_io_io_remove_timeout;
}

GType
inf_standalone_io_get_type(void)
{
  static GType standalone_io_type = 0;

  if(!standalone_io_type)
  {
    static const GTypeInfo standalone_io_type_info = {
      sizeof(InfStandaloneIoClass),   /* class_size */
      NULL,                           /* base_init */
      NULL,                           /* base_finalize */
      inf_standalone_io_class_init,   /* class_init */
      NULL,                           /* class_finalize */
      NULL,                           /* class_data */
      sizeof(InfStandaloneIo),        /* instance_size */
      0,                              /* n_preallocs */
      inf_standalone_io_init,         /* instance_init */
      NULL                            /* value_table */
    };

    static const GInterfaceInfo io_info = {
      inf_standalone_io_io_init,
      NULL,
      NULL
    };

    standalone_io_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfStandaloneIo",
      &standalone_io_type_info,
      0
    );

    g_type_add_interface_static(
      standalone_io_type,
      INF_TYPE_IO,
      &io_info
    );
  }

  return standalone_io_type;
}

/**
 * inf_standalone_io_new:
 *
 * Creates a new #InfStandaloneIo.
 **/
InfStandaloneIo*
inf_standalone_io_new(void)
{
#ifndef G_OS_WIN32
  GObject* object;
  object = g_object_new(INF_TYPE_STANDALONE_IO, NULL);
  return INF_STANDALONE_IO(object);
#else /* !G_OS_WIN32 */
  g_warning("InfStandaloneIo is not implemented on Win32");
  return NULL;
#endif
}

/**
 * inf_standalone_io_iteration:
 * @io: A #InfStandaloneIo.
 *
 * Performs a single iteration of @io. The call will block until a first
 * event has occured. Then, it will process that event and return.
 **/
void
inf_standalone_io_iteration(InfStandaloneIo* io)
{
#ifndef G_OS_WIN32
  g_return_if_fail(INF_IS_STANDALONE_IO(io));
  inf_standalone_io_iteration_impl(io, -1);
#else /* !G_OS_WIN32 */
  g_warning("InfStandaloneIo is not implemented on Win32");
#endif
}

/**
 * inf_standalone_io_iteration_timeout:
 * @io: A #InfStandaloneIo.
 * @timeout: Maximum number of milliseconds to block.
 *
 * Performs a single iteration of @io. The call will block until either an
 * event occured or @timeout milliseconds have elapsed. If an event occured,
 * the event will be processed before returning.
 **/
void
inf_standalone_io_iteration_timeout(InfStandaloneIo* io,
                                    guint timeout)
{
#ifndef G_OS_WIN32
  g_return_if_fail(INF_IS_STANDALONE_IO(io));
  inf_standalone_io_iteration_impl(io, (int)timeout);
#else /* !G_OS_WIN32 */
  g_warning("InfStandaloneIo is not implemented on Win32");
#endif
}

/**
 * inf_standalone_io_loop:
 * @io: A #InfStandaloneIo.
 *
 * This call will cause @io to wait for events and process them, but not
 * return until inf_standalone_io_loop_quit() is called.
 **/
void
inf_standalone_io_loop(InfStandaloneIo* io)
{
#ifndef G_OS_WIN32
  InfStandaloneIoPrivate* priv;

  g_return_if_fail(INF_IS_STANDALONE_IO(io));
  priv = INF_STANDALONE_IO_PRIVATE(io);

  g_return_if_fail(priv->loop_running == FALSE);
  priv->loop_running = TRUE;

  while(priv->loop_running == TRUE)
    inf_standalone_io_iteration_impl(io, -1);
#else /* !G_OS_WIN32 */
  g_warning("InfStandaloneIo is not implemented on Win32");
#endif
}

/**
 * inf_standalone_io_loop_quit:
 * @io: A #InfStandaloneIo.
 *
 * Exits a loop in which @io is running through a call to
 * inf_standalone_io_loop().
 **/
void
inf_standalone_io_loop_quit(InfStandaloneIo* io)
{
#ifndef G_OS_WIN32
  InfStandaloneIoPrivate* priv;

  g_return_if_fail(INF_IS_STANDALONE_IO(io));
  priv = INF_STANDALONE_IO_PRIVATE(io);

  g_return_if_fail(priv->loop_running == TRUE);
  priv->loop_running = FALSE;
#else /* !G_OS_WIN32 */
  g_warning("InfStandaloneIo is not implemented on Win32");
#endif
}

/**
 * inf_standalone_io_loop_running:
 * @io: A #InfStandaloneIo.
 *
 * Returns whether @io runs currently in a loop initiated with
 * inf_standalone_io_loop().
 *
 * Return Value: Whether @io runs in a loop.
 **/
gboolean
inf_standalone_io_loop_running(InfStandaloneIo* io)
{
#ifndef G_OS_WIN32
  InfStandaloneIoPrivate* priv;

  g_return_val_if_fail(INF_IS_STANDALONE_IO(io), FALSE);
  priv = INF_STANDALONE_IO_PRIVATE(io);

  return priv->loop_running;
#else /* !G_OS_WIN32 */
  g_warning("InfStandaloneIo is not implemented on Win32");
  return FALSE;
#endif
}

/* vim:set et sw=2 ts=2: */
