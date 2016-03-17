#ifndef XXX_EVENT_H
#define XXX_EVENT_H

#include "Callbacks.h"


#include <jalson/jalson.h>

#include <functional>

namespace XXX {

  struct Request_CB_Data;


/*
  TODO: perhaps shoudl have a better taxonomy of event classes? current thoughts:

  * need a base class

  * represent event from an external peer (ie, will have source SID), and should
    have a source message.

  * represent an inbound request event
    -> arrival of CALL, REGISTER, INVOCATION
    -> inbound means the event was created inside a session, prob from IO callback

  * represent an output response event
    -> outbound means the event is destined for a session, then onto its IO

  * somehow represent outbound events?


 */

class event
{
public:

  enum Type
  {
    eNone = 0,
    session_state_event,
    outbound_call_event,
    outbound_response_event,
    outbound_message,
    inbound_publish,
    tcp_active_connect_event,
    outbound_subscribe,
    inbound_subscribed
  } type;

  enum Mode
  {
    eModeInvalid = 0,
    eInbound,
    eOutbound
  } mode;

  int msg_type; // WAMP message type

  session_handle src;
  jalson::json_array ja;

  jalson::json_array orig_req; // populated for reply/error to a previous request

  Request_CB_Data* cb_data; // valid for responses to request

  unsigned int internal_req_id;
  void * user;

  event(Type t = eNone)
    : type(t),
      cb_data( nullptr ),
      internal_req_id(0),
      user(nullptr)
  {
  }

  virtual ~event();
};


/* new style event */
class inbound_message_event : public event
{
public:
  jalson::json_array msg;

  inbound_message_event(int __msgtype)
  {
    this->mode =  event::eInbound;
    this->msg_type = __msgtype;
  }
};

struct tcp_active_connect_event : public event
{
  tcp_connect_attempt_cb user_cb;
  void* user_data;
  int status;  /* 0 is no error */
  tcp_active_connect_event(tcp_connect_attempt_cb __user_cb,
                    void* ud,
                    int st)
    : event( event::tcp_active_connect_event ),
      user_cb(__user_cb),
      user_data(ud),
      status(st)
  {
  }
};


struct session_state_event : public event
{
  bool is_open;

  session_state_event(bool __session_open)
  : event( event::session_state_event ),
    is_open( __session_open )
  {
  }
};


/* Another attempt at a generic outbound event */
struct outbound_message : public event
{
  session_handle destination;
  int message_type;

  outbound_message()
    : event( event::outbound_message )
  {
  }
};

/* New style event.  Using a specific class to capture response events, rather
 * than further overloading the base event class. */
struct outbound_response_event : public event
{
  session_handle destination;
  int response_type;
  int request_type;
  t_request_id reqid;

  jalson::json_object options;
  std::string error_uri;  // used only for ERROR

  rpc_args args;

  outbound_response_event()
    : event( event::outbound_response_event )
  {
  }
};



struct outbound_call_event : public event
{
  outbound_call_event()
    : event( event::outbound_call_event ),
      cb_user_data( nullptr )
  {
  }

  session_handle dest;
  std::string rpc_name;
  call_user_cb cb;
  void * cb_user_data;
  rpc_args args;
  unsigned int internal_req_id;
};


struct ev_inbound_publish : public event
{
  bool is_internal;
  std::string uri;
  jalson::json_value patch;

  ev_inbound_publish(bool source_is_internal,
                     const std::string & __topic_uri,
                     const jalson::json_value& __patch)
  :  event( event::inbound_publish ),
     is_internal( source_is_internal ),
     uri( __topic_uri ),
     patch( __patch )
  {
  }
};

struct ev_outbound_subscribe : public event
{
  session_handle dest;
  std::string uri;
  int internal_req_id;

  ev_outbound_subscribe(const std::string & __topic_uri)
  :  event( event::outbound_subscribe ),
     uri( __topic_uri )
  {
  }
};

struct ev_inbound_subscribed : public event
{
  session_handle src;
  unsigned int internal_req_id;

  ev_inbound_subscribed()
    : event( event::inbound_subscribed )
  {
  }

};

} // namespace xxx

#endif
