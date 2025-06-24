#ifndef cwTracer_h
#define cwTracer_h

namespace cw
{
  namespace tracer
  {
    typedef handle<struct tracer_str> handle_t;

    enum {
      kBegEvtId=1,
      kEndEvtId=2,
      kDataEvtId=3,
    };

    rc_t create( handle_t& hRef, const object_t* cfg );

    // If 'enable_fl' is false the object is assumed to be disabled and will not log any information.
    // This is appropriate for release builds.
    rc_t create( handle_t& hRef, unsigned max_trace_cnt, unsigned max_msg_cnt, bool enable_fl, bool activate_fl, const char* fname );

    rc_t destroy( handle_t& hRef );

    // start/stop event recording.
    rc_t activate( handle_t h, bool activate_fl );

    // Register a trace and get a trace id.
    rc_t register_trace( handle_t h, const char* label, unsigned label_id, unsigned& trace_id_ref );

    // Log the time of a trace event.
    rc_t log_trace_time( handle_t h, unsigned trace_id, unsigned event_id, unsigned user_data_0, unsigned user_data_1 );
    rc_t log_trace_data( handle_t h, unsigned trace_id, unsigned event_id, unsigned user_data_0, unsigned user_data_1 );

    rc_t write( handle_t h );

    void set_global_handle( handle_t h );
    handle_t global_handle();
    
  }

}

#ifdef cwTRACER
#define TRACE_ACTIVATE( fl )                       cw::tracer::activate( cw::tracer::global_handle(), fl )
#define TRACE_REG( label, label_id, trace_id_ref ) cw::tracer::register_trace( cw::tracer::global_handle(), label, label_id, trace_id_ref )
#define TRACE_TIME( trace_id, evt, ud0, ud1 )      cw::tracer::log_trace_time( cw::tracer::global_handle(), trace_id, evt, ud0, ud1 )
#define TRACE_DATA( trace_id, evt, ud0, ud1 )      cw::tracer::log_trace_data( cw::tracer::global_handle(), trace_id, evt, ud0, ud1 )
#else
#define TRACE_ACTIVATE( fl )
#define TRACE_REG( label, label_id, trace_id_ref )
#define TRACE_TIME( trace_id, evt, ud0, ud1 )
#define TRACE_DATA( trace_id, evt, ud0, ud1 )
#endif

#endif
