#ifndef cwIoFlowCtl_H
#define cwIoFlowCtl_H

namespace cw
{
  namespace io_flow_ctl
  {

    typedef handle< struct io_flow_ctl_str > handle_t;

    rc_t create(  handle_t& hRef, io::handle_t ioH, const char* flow_cfg_fn );
    rc_t create(  handle_t& hRef, io::handle_t ioH, const object_t* flow_cfg );
    rc_t destroy( handle_t& hRef );

    unsigned    program_count( handle_t h);
    const char* program_title( handle_t h, unsigned pgm_idx );
    unsigned    program_index( handle_t h, const char* pgm_title);
    bool        program_is_nrt(handle_t h, unsigned pgm_idx);
    rc_t        program_load(  handle_t h, unsigned pgm_idx );

    // Return the index of the currently loaded program or kInvalidIdx if no program is loaded.
    unsigned    program_current_index( handle_t h );

    // Reset the the current program to it's initial state.
    rc_t        program_reset( handle_t h );
    
    // Is the currently loaded program in non-real-time mode
    bool        is_program_nrt( handle_t h );

    // Execute the currently loaded non-real-time program to completion.
    rc_t        exec_nrt( handle_t h );
    
    // Return the count of network presets associated with the current program.
    unsigned    preset_count( handle_t h );
    const char* preset_title( handle_t h, unsigned preset_idx );
    rc_t        preset_apply( handle_t h, unsigned preset_idx );


    // Handle an incoming IO msg.
    rc_t exec( handle_t h, const io::msg_t& msg );

    void report( handle_t h );
    void print_network( handle_t h );
 
    
  }
  
}



#endif
