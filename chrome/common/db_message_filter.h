// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_DB_MESSAGE_FILTER_H_
#define CHROME_COMMON_DB_MESSAGE_FILTER_H_

#include "base/scoped_ptr.h"
#include "base/waitable_event.h"
#include "chrome/common/id_map.h"
#include "ipc/ipc_channel_proxy.h"

class Lock;
class MessageLoop;

namespace base {
class AtomicSequenceNumber;
}

namespace IPC {
class Channel;
}

// A thread-safe message filter used to send IPCs from DB threads and
// process replies from the browser process.
//
// This class should not be instantianted anywhere but RenderThread::Init().
// It is meant to be a singleton in each renderer process. To get the
// singleton, use DBMessageFilter::GetInstance().
class DBMessageFilter : public IPC::ChannelProxy::MessageFilter {
 public:
  // Creates a new DBMessageFilter instance.
  DBMessageFilter();

  // Returns the DBMessageFilter singleton created in this renderer process.
  static DBMessageFilter* GetInstance();

  // Returns a ID that uniquely identifies each message that will be sent
  // using the SendAndWait() method.
  virtual int GetUniqueID();

  // Posts a task to the IO thread to send the given message to the browser.
  //
  // message - The message.
  virtual void Send(IPC::Message* message);

  // Sends a message and blocks the current thread until a reply for that
  // message is received or the renderer process is about to be destroyed.
  //
  // Returns the result from the reply message or the default value if the
  // renderer process is being destroyed, or the message could not be sent.
  //
  // message - The message to be sent.
  // message_id - An ID that uniquely identifies this message.
  // default_result - The default value; returned when the renderer process
  //                  is being destroyed before the reply got back, or if
  //                  the message could not be sent.
  template<class ResultType>
  ResultType SendAndWait(IPC::Message* message, int message_id,
    ResultType default_result) {
    ResultType result;
    base::WaitableEvent waitable_event(false, false);
    DBMessageState state =
      { reinterpret_cast<intptr_t>(&result), &waitable_event };
    messages_awaiting_replies_->AddWithID(&state, message_id);

    Send(message);

    base::WaitableEvent* events[2] = { shutdown_event_, &waitable_event };
    size_t event_index = base::WaitableEvent::WaitMany(events, 2);
    return (event_index ? result : default_result);
  }

  // Processes the incoming message from the browser process.
  //
  // message - The message.
  virtual bool OnMessageReceived(const IPC::Message& message);

 private:
  // The state we store for each message we send
  struct DBMessageState {
    intptr_t result_address_;
    base::WaitableEvent* waitable_event_;
  };

  // This is a RefCount'ed class, do not allow anybody to destroy
  // instances of this class
  virtual ~DBMessageFilter();

  // Invoked when this filter is added to a channel.
  //
  // channel - The channel this filter was added to.
  virtual void OnFilterAdded(IPC::Channel* channel);

  // Called when the channel encounters a problem. The filter should
  // clean up its internal data and not accept any more messages.
  virtual void OnChannelError();

  // Called when the channel is closing. The filter should clean up
  // its internal data and not accept any more messages.
  virtual void OnChannelClosing();

  // Processes the reply to a DB request
  template<class ResultType>
  void OnResponse(int32 message_id, ResultType result) {
    DBMessageState *state = messages_awaiting_replies_->Lookup(message_id);
    if (state) {
      messages_awaiting_replies_->Remove(message_id);
      *reinterpret_cast<ResultType*>(state->result_address_) = result;
      state->waitable_event_->Signal();
    }
  }

  // The message loop of the IO thread
  MessageLoop* io_thread_message_loop_;

  // The channel this filter was added to
  IPC::Channel* channel_;

  // A lock around the channel
  scoped_ptr<Lock> channel_lock_;

  // The shutdown event
  base::WaitableEvent* shutdown_event_;

  // The list of messages awaiting replies
  // For each such message we store a DBMessageState instance
  scoped_ptr<IDMap<DBMessageState> > messages_awaiting_replies_;

  // A thread-safe unique number generator
  scoped_ptr<base::AtomicSequenceNumber> unique_id_generator_;

  // The singleton
  static DBMessageFilter* instance_;
};

#endif // CHROME_COMMON_DB_MESSAGE_FILTER_H_
