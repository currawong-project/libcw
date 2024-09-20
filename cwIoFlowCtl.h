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

    // Query the available programs from the 'flow_cfg' file.
    unsigned    program_count( handle_t h);
    const char* program_title( handle_t h, unsigned pgm_idx );
    unsigned    program_index( handle_t h, const char* pgm_title);

    // Create the parse the program but do not instantiate the network.
    rc_t        program_load(  handle_t h, unsigned pgm_idx );

    // Return the index of the currently loaded program or kInvalidIdx if no program is loaded.
    unsigned    program_current_index( handle_t h );

    // Is the currently loaded program in non-real-time mode
    bool        is_program_nrt( handle_t h );

    // Return the count of network presets and the associated labels for the currently loaded program.
    unsigned    program_preset_count( handle_t h );
    const char* program_preset_title( handle_t h, unsigned preset_idx );

    // Create the network and prepare to enter runtime.
    rc_t        program_initialize( handle_t h, unsigned preset_idx=kInvalidIdx );
    
    // Execute the currently loaded non-real-time program to completion.
    rc_t        exec_nrt( handle_t h );
    
    // Handle an incoming IO msg. This is the main point of entry for executing
    // real-time programs.
    rc_t exec( handle_t h, const io::msg_t& msg );

    // Is the current program loaded, initialized and not yet complete.
    bool is_executable( handle_t h );

    // The current program has completed.
    bool is_exec_complete( handle_t h );

    void report( handle_t h );
    void print_network( handle_t h );
 
    
  }
  
}



#endif
