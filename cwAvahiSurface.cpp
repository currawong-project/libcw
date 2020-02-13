
#include <avahi-client/client.h>
#include <avahi-client/publish.h>

#include <avahi-common/alternative.h>
#include <avahi-common/thread-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-common/timeval.h>
#include <avahi-common/strlst.h>


#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTime.h"
#include "cwThread.h"
#include "cwTcpSocket.h"

using namespace cw;
using namespace cw::net;

#define INSTANCE_NAME  "MC Mix"
#define SERVICE_NAME   "_EuConProxy._tcp"
#define SERVICE_PORT   49168
#define SERVICE_HOST   nullptr //"Euphonix-MC-38C9863744E7.local"
#define ENET_INTERFACE "ens9"
#define SERVICE_TXT_0  "lmac=38-C9-86-37-44-E7"
#define SERVICE_TXT_1  "dummy=0"
#define HOST_MAC "hmac=00-E0-4C-A9-A4-8D" // mbp19 enet MAC

typedef struct app_str
{
  AvahiEntryGroup   *group         = nullptr;
  AvahiThreadedPoll *poll          = nullptr;
  char              *name          = nullptr;
  unsigned           instanceId    = 0;
  socket::handle_t   tcpH;
  thread::handle_t   tcpThreadH;
  unsigned           recvBufByteN  = 4096;
  unsigned           protocolState = 0;
  unsigned           txtXmtN       = 0;
  time::spec_t       t0;
  
  
} app_t;
  
static void create_services(app_t* app, AvahiClient *c);

void errorv( app_t* app, const char* fmt, va_list vl )
{
  vprintf(fmt,vl);
}

void logv( app_t* app, const char* fmt, va_list vl )
{
  vprintf(fmt,vl);  
}

void error( app_t* app, const char* fmt, ... )
{
  va_list vl;
  va_start(vl,fmt);
  errorv( app, fmt, vl );
  va_end(vl);
}

void rpt( app_t* app, const char* fmt, ... )
{
  va_list vl;
  va_start(vl,fmt);
  logv( app, fmt, vl );
  va_end(vl);
}

void choose_new_service_name( app_t* app )
{
  char buf[255];
  app->instanceId += 1;
  snprintf(buf,sizeof(buf),"%s - %i", INSTANCE_NAME, app->instanceId);

  char* n = avahi_strdup(buf);
  avahi_free(app->name);
  app->name = n;

  rpt(app,"Service name collision, renaming service to '%s'\n", app->name);
}

static void entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, void *userdata)
{
  app_t* app = (app_t*)userdata;
  
  assert(g == app->group || app->group == nullptr);
  app->group = g;

  // Called whenever the entry group state changes

  switch (state)
  {
    case AVAHI_ENTRY_GROUP_ESTABLISHED :
      // The entry group has been established successfully 
      rpt( app, "Service '%s' successfully established.\n", app->name);
      break;

    case AVAHI_ENTRY_GROUP_COLLISION :
      {

        // A service name collision with a remote service  happened. Let's pick a new name.
        choose_new_service_name(app);

        // And recreate the services
        create_services(app,avahi_entry_group_get_client(g));
        break;
      }

    case AVAHI_ENTRY_GROUP_FAILURE :

      error(app,"Entry group failure: %s\n", avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))));

      /* Some kind of failure happened while we were registering our services */
      avahi_threaded_poll_quit(app->poll);
      break;

    case AVAHI_ENTRY_GROUP_UNCOMMITED:
    case AVAHI_ENTRY_GROUP_REGISTERING:
      ;
  }
}

static void create_services(app_t* app, AvahiClient *c)
{
  int ret;
  AvahiPublishFlags flags = (AvahiPublishFlags)0;
    
  assert(c);

  // If this is the first time we're called, create a new entry group
  if (!app->group)
  {
    if (!(app->group = avahi_entry_group_new(c, entry_group_callback, app)))
    {
      error(app,"avahi_entry_group_new() failed: %s\n", avahi_strerror(avahi_client_errno(c)));
      goto fail;
    }
  }
    
  // If the group is empty (either because it was just created, or because it was reset previously, add our entries. 
  if (avahi_entry_group_is_empty(app->group))
  {
    rpt(app,"Adding service '%s'\n", app->name);

    // Add the service to the group
    if ((ret = avahi_entry_group_add_service(app->group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, flags, app->name, SERVICE_NAME, nullptr, SERVICE_HOST, SERVICE_PORT, SERVICE_TXT_0, SERVICE_TXT_1, nullptr)) < 0)
    {
      if (ret == AVAHI_ERR_COLLISION)
        goto collision;

      error(app, "Failed to add _ipp._tcp service: %s\n", avahi_strerror(ret));
      goto fail;
    }

    // Tell the server to register the service 
    if ((ret = avahi_entry_group_commit(app->group)) < 0)
    {
      error(app,"Failed to commit entry group: %s\n", avahi_strerror(ret));
      goto fail;
    }
  }

  return;

 collision:

  // A service name collision with a local service happened. Pick a new name.
  choose_new_service_name(app);

  avahi_entry_group_reset(app->group);

  create_services(app,c);
  return;

 fail:
  avahi_threaded_poll_quit(app->poll);
}


static void client_callback(AvahiClient *c, AvahiClientState state, void * userdata)
{
  assert(c);

  app_t* app = (app_t*)userdata;

  // Called whenever the client or server state changes 

  switch (state)
  {
    case AVAHI_CLIENT_S_RUNNING:
      // The server has startup successfully and registered its host
      // name on the network, so it's time to create our services
      create_services(app,c);
      break;

    case AVAHI_CLIENT_FAILURE:
      error(app,"Client failure: %s\n", avahi_strerror(avahi_client_errno(c)));
      avahi_threaded_poll_quit(app->poll);
      break;

    case AVAHI_CLIENT_S_COLLISION:
      // Let's drop our registered services. When the server is back
      // in AVAHI_SERVER_RUNNING state we will register them
      // again with the new host name.
      rpt(app,"S Collision\n");

    case AVAHI_CLIENT_S_REGISTERING:

      rpt(app,"S Registering\n");
          
      // The server records are now being established. This
      // might be caused by a host name change. We need to wait
      // for our own records to register until the host name is
      // properly esatblished.
      if (app->group)
        avahi_entry_group_reset(app->group);

      break;

    case AVAHI_CLIENT_CONNECTING:
      ;
  }
}


rc_t _send_response( app_t* app, const unsigned char* buf, unsigned bufByteN )
{
  rc_t rc;
  
  if((rc = socket::send( app->tcpH, buf, bufByteN )) != kOkRC )
  {
    error(app,"Send failed.");
  }

  return rc;
}
rc_t send_response1( app_t* app )
{
  // wifi: 98 5A EB 89 BA AA
  // enet: 38 C9 86 37 44 E7
          
  unsigned char buf[] =
    { 0x0b,0x00,0x00,0x00,0x00,0x00,0x00,0x50,0x00,0x02,0x03,0xfc,0x01,0x05,
      0x06,0x00,
      0x38,0xc9,0x86,0x37,0x44,0xe7,
      0x01,0x00,
      0xc0,0xa8,0x00,0x44,
      0x00,0x00,
      0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
      0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
      0x03,0xff,0x00,0x30,0x08,0x00,0x00,0x80,0x00,0x40,0x01,0x01,0x00,0x00,0x00,0x00,
      0x00,0x00
    };
  
  return _send_response(app,buf,sizeof(buf));
}

rc_t send_response2( app_t* app )
{
  unsigned char buf[] = { 0x0d,0x00,0x00,0x00,0x00,0x00,0x00,0x08 };

  return _send_response(app,buf,sizeof(buf));
}

rc_t send_heart_beat( app_t* app )
{
  unsigned char buf[] = { 0x03,0x00,0x00,0x00 };
  return _send_response(app,buf,sizeof(buf));
}

rc_t send_txt( app_t* app, bool updateFl=true )
{
  rc_t rc = kOkRC;
  int ret;
  
  const char* array[] =
  {
   "lmac=38-C9-86-37-44-E7",
   "host=mbp19",
   "hmac=BE-BD-EA-31-F9-88",  
   "dummy=1"
  };
    
  AvahiStringList* list = avahi_string_list_new_from_array(array,4);
  if( updateFl )
    ret = avahi_entry_group_update_service_txt_strlst(app->group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, (AvahiPublishFlags)0, app->name, SERVICE_NAME, nullptr,  list);
  else
    ret = avahi_entry_group_add_service_strlst(app->group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, (AvahiPublishFlags)0, app->name, SERVICE_NAME, nullptr, nullptr, SERVICE_PORT, list);
    
  if(ret < 0)
  {
    error(app,"Failed to %s entry group text: %s\n", updateFl ? "update" : "add", avahi_strerror(ret));
    goto fail;    
  }

  avahi_string_list_free(list);

 fail:
  
  return rc;
}

bool tcpReceiveCallback( void* arg )
{
  app_t*           app       = static_cast<app_t*>(arg);
  socket::handle_t sockH     = app->tcpH;
  char             buf[ app->recvBufByteN ];
  unsigned         readByteN = 0;
  rc_t             rc        = kOkRC;
  time::spec_t t1;

  if( !socket::isConnected(sockH) )
  {
    if((rc = socket::accept( sockH )) == kOkRC )
    {
      rpt(app,"TCP connected.\n");
    }
  }
  else
  {
    if((rc = socket::receive( sockH, buf, app->recvBufByteN, &readByteN, nullptr )) == kOkRC || rc == kTimeOutRC )
    {
      if( rc == kTimeOutRC )
      {
        
      }
      else
        if( readByteN > 0 )
        {
          unsigned* h = (unsigned*)buf;
          unsigned id = h[0];
          switch( app->protocolState )
          {
            case 0:
              if( id == 10 )
              {
                send_response1(app);
                sleepMs(20);
                send_heart_beat(app);
                app->protocolState+=1;
              }
              break;
              
            case 1:
              {
                if( buf[0] == 0x0c )
                {
                  send_response2(app);
                  app->protocolState+=1;
                  time::get(app->t0);
                }
              }
              break;

            case 2:            
              {
                time::get(t1);
                if( time::elapsedMs( &app->t0, &t1 ) >= 4000 )
                {
                  send_heart_beat(app);
                  app->t0 = t1;
                }
              }
              break;
          
          }
        }
    }
  }               
  return true;
}


int main( int argc, const char* argv[] )
{
  
  AvahiClient    *client = nullptr;
  int             err_code;
  int             ret    = 1;
  const unsigned  sbufN  = 31;
  char            sbuf[ sbufN+1 ];
  app_t           app;
  rc_t        rc;
  unsigned        tcpTimeOutMs = 50;

  cw::log::createGlobal();
  
  // create the  TCP socket
  if((rc = socket::create(
        app.tcpH,
        SERVICE_PORT,
        socket::kTcpFl | socket::kBlockingFl | socket::kStreamFl | socket::kListenFl,
        tcpTimeOutMs,
        NULL,
        socket::kInvalidPortNumber )) != kOkRC )
  {    
    rc = cwLogError(rc,"mDNS TCP socket create failed.");
    goto errLabel;
  }

  unsigned char mac[6];
  socket::get_mac( app.tcpH, mac, nullptr, ENET_INTERFACE );
  for(int i=0; i<6; ++i)
    printf("%02x:",mac[i]);

  
  // create the TCP listening thread
  if((rc = thread::create( app.tcpThreadH, tcpReceiveCallback, &app )) != kOkRC )
    goto errLabel;
  
  // Allocate Avahi thread 
  if (!(app.poll = avahi_threaded_poll_new()))
  {
    error(&app,"Failed to create simple poll object.\n");
    goto errLabel;
  }

  // Assign the service name
  app.name = avahi_strdup(INSTANCE_NAME);

  // Allocate a new client
  if((client = avahi_client_new(avahi_threaded_poll_get(app.poll), (AvahiClientFlags)0, client_callback, &app, &err_code)) == nullptr )
  {
    error(&app,"Failed to create client: %s\n", avahi_strerror(err_code));
    goto errLabel;
  }
  
  // start the tcp thread
  if((rc = thread::unpause( app.tcpThreadH )) != kOkRC )
    goto errLabel;

  // start the avahi thread
  avahi_threaded_poll_start(app.poll);

  while( true )
  {
    printf("? ");
    if( std::fgets(sbuf,sbufN,stdin) == sbuf )
    {
      if( strcmp(sbuf,"quit\n") == 0)
        break;
    }
  }

  avahi_threaded_poll_stop(app.poll);

  ret = 0;
  
 errLabel:
  
  //if (client)
  //  avahi_client_free(client);

  thread::destroy(app.tcpThreadH);
  socket::destroy(app.tcpH);
  
  if (app.poll)
    avahi_threaded_poll_free(app.poll);

  avahi_free(app.name);

  
  cw::log::destroyGlobal();

  return ret;
}
