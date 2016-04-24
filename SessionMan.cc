
#include "SessionMan.h"

#include "event_loop.h"
#include "Session.h"

#include "WampTypes.h"
#include "Logger.h"

#include <string.h>

#define MAX_PENDING_OPEN_SECS 2

namespace XXX
{

SessionMan::SessionMan(Logger* logptr, event_loop& evl)
  : __logptr(logptr),
    m_evl(evl)
{
  m_evl.set_session_man( this );
  m_sessions.m_next = 1;
}


SessionMan::~SessionMan()
{
  std::lock_guard<std::mutex> guard(m_sessions.lock);


  for (auto & item  : m_sessions.active)
    delete item.second;
  for (auto & item : m_sessions.closed)
    delete item;
}


Session* SessionMan::create_session(IOHandle * iohandle, bool is_passive,
                                    t_connection_id user_conn_id,
                                    std::string realm)
{
  /* IO thread */

  std::lock_guard<std::mutex> guard(m_sessions.lock);
  SID sid (m_sessions.m_next++);  // TODO find a unqiue numner, dont use 0,
  // and chekc is not altready in the map.
  // Need to handle case where we wrap around
  // and have to skip zero ... actuall, to handle this, set start number to minus 1

  // TODO: its not the place here to configure the newly created session .
  // That should be done by the caller of this function.
  Session * sptr;
  sptr = new Session( sid,
                      __logptr,
                      iohandle,
                      this,
                      m_evl,
                      is_passive,
                      user_conn_id,
                      realm);

  m_sessions.active[ sid ] = sptr;

  _INFO_( "session created, id:" << sid );
  if (!is_passive) sptr->initiate_handshake();

  return sptr;
}


void SessionMan::heartbeat_all()
{
  jalson::json_array msg;

  msg.push_back(HEARTBEAT);
  std::lock_guard<std::mutex> guard(m_sessions.lock);

  for (auto i : m_sessions.active)
  {
    if ( i.second->is_open() && i.second->hb_interval_secs())
    {
      // do heartbeat check on an open session
      if (i.second->duration_since_last() > i.second->hb_interval_secs()*3)
      {
        // expire sessions which appear inactive
          _WARN_("closing session due to inactivity " << i.second->hb_interval_secs() << ", " << i.second->duration_since_last());
          i.second->close();
      }
      else
      {
        i.second->send_msg(msg);
      }
    }

    if (i.second->is_pending_open())
    {
      if (i.second->duration_pending_open() >= MAX_PENDING_OPEN_SECS )
      {
        // expire sessions which have spent too long in pending-open
        _WARN_("closing session due to incomplete handshake");
        i.second->close();
      }
    }
  }
}


void SessionMan::close_all()
{
  std::lock_guard<std::mutex> guard(m_sessions.lock);

  for (auto i : m_sessions.active)
  {
    i.second->close();
  }
}


void SessionMan::session_closed(Session& s)
{
  /* IO thread */

  _INFO_("SessionMan::session_closed");
  std::lock_guard<std::mutex> guard(m_sessions.lock);

  // TODO tryong to do an immediate delete
  m_sessions.closed.push_back( & s );

  for (auto it = m_sessions.active.begin(); it != m_sessions.active.end(); )
  {
    if (it->second == &s)
      m_sessions.active.erase( it++ );
    else
      it++;
  }

  s.remove_listener();


  // TODO: here, could push an event
}


//----------------------------------------------------------------------


void SessionMan::send_to_session(session_handle handle,
                                 build_message_cb_v4 msg_builder)
{
  std::lock_guard<std::mutex> guard(m_sessions.lock);

  auto sp = handle.lock();
  if (!sp)
  {
    _WARN_("failed to lock the session handle");
    return;
  }
  SID dest( *sp );


  if (dest == SID())
  {
    _WARN_("ignoring attempt to send to session with id 0");
    return;
  }

  auto it = m_sessions.active.find( dest );

  if (it != m_sessions.active.end())
  {
    it->second->send_msg( msg_builder );
  }
  else
  {
    std::ostringstream os;
    os << "session send failed; cannot find session with id " << dest.unique_id();
    throw std::runtime_error( os.str() );
  }
}

//----------------------------------------------------------------------

void SessionMan::send_to_session(session_handle handle, jalson::json_array& msg)
{
  std::lock_guard<std::mutex> guard(m_sessions.lock);
  send_to_session_impl(handle, msg);
}

//----------------------------------------------------------------------

void SessionMan::send_to_session_impl(session_handle handle,
                                      jalson::json_array& msg)
{
  auto sp = handle.lock();
  if (!sp)
  {
    _WARN_("failed to lock the session handle");
    return;
  }

  SID dest( *sp );
  if (dest == SID())
  {
    _WARN_("ignoring attempt to send to session with id 0");
    return;
  }

  bool is_final = false;

  auto it = m_sessions.active.find( dest );
  if (it != m_sessions.active.end())
  {
    it->second->send_msg(msg, is_final);
  }
  else
  {
    // TODO: needs to throw?  Because causes problem for when we are looping of
    // a list of sessions to send on.
    std::ostringstream os;
    os << "session send failed; cannot find session with id " << dest;
    throw std::runtime_error( os.str() );
  }
}


void SessionMan::send_to_session(const std::vector<session_handle>& handles,
                                 jalson::json_array& msg)
{
  std::lock_guard<std::mutex> guard(m_sessions.lock);

  for (auto & handle : handles)
  {
    try {
      send_to_session_impl(handle, msg);
    }
    catch (const std::exception& e)
    {
      _ERROR_("exception during msg send: " << e.what());
    }
  }
}


//----------------------------------------------------------------------

void SessionMan::send_request(session_handle handle_weak,
                              int request_type,
                              unsigned int internal_req_id,
                              build_message_cb_v2 msg_builder)
{
  std::lock_guard<std::mutex> guard(m_sessions.lock);

  auto sp = handle_weak.lock();
  if (!sp)
  {
    _WARN_("failed to lock the session handle");
    return;
  }

  SID dest( *sp );
  if (dest == SID())
  {
    _WARN_("ignoring attempt to send to session with id 0");
    return;
  }


  auto it = m_sessions.active.find( dest );
  if (it != m_sessions.active.end())
  {
    it->second->send_request( request_type, internal_req_id, msg_builder );
  }
  else
  {
    throw std::runtime_error("cannot send to session; session not found");
  }
}

//----------------------------------------------------------------------

void SessionMan::handle_event(ev_session_state_event* ev)
{
  Session* sptr = NULL;

  {
    std::lock_guard<std::mutex> guard(m_sessions.lock);

    auto sp = ev->src.lock();
    if (!sp) return;
    SID sid ( *sp );


    auto it = m_sessions.active.find( sid );

    if (it == m_sessions.active.end())
    {
      _ERROR_("ignoring session state event for non active session sid:" << sid);
      return;
    }

    sptr = it->second;

    if (ev->is_open == false)
    {
      m_sessions.active.erase( it );
      m_sessions.closed.push_back(sptr);
    }
  }

  if (m_session_event_cb) m_session_event_cb(ev);
}

//----------------------------------------------------------------------

void SessionMan::set_session_event_listener(session_state_cb cb)
{
  m_session_event_cb = cb;
}

//----------------------------------------------------------------------

void SessionMan::handle_housekeeping_event()
{
  this->heartbeat_all();

  {
    std::lock_guard<std::mutex> guard(m_sessions.lock);

    // TODO: I can move the deletion to outside of the critical section.
//    _INFO_("deleting expired sessions: " << m_sessions.closed.size());
    for (auto & i : m_sessions.closed)
    {
      delete i;
    }
    m_sessions.closed.clear();
  }
}

//----------------------------------------------------------------------

bool SessionMan::session_is_open(session_handle sh) const
{
  std::lock_guard<std::mutex> guard(m_sessions.lock);

  auto sp = sh.lock();
  if (!sp) return false;
  SID sid ( *sp );

  auto it = m_sessions.active.find( sid );

  if (it == m_sessions.active.end()) return false;

  Session * sptr = it->second;
  return sptr->is_open();
}

}
