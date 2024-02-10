namespace cw
{
  namespace midi
  {
    namespace file_dev
    {
      typedef handle<struct file_dev_str> handle_t;

      rc_t create( handle_t& hRef, const char* labelA[], unsigned max_file_cnt );
      rc_t destroy( handle_t& hRef );

      unsigned file_count( handle_t h );

      rc_t open_midi_file( handle_t h, unsigned file_idx, const char* fname );
      
      rc_t seek_to_event( handle_t h, unsigned file_idx, unsigned msg_idx );

      rc_t start( handle_t h );
      rc_t stop( handle_t h );

      rc_t enable_file(  handle_t h, unsigned file_idx );
      rc_t disable_file( handle_t h,unsigned file_idx );

      int file_descriptor( handle_t h );
      
      typedef struct msg_str
      {
        const file::trackMsg_t* msg;
        unsigned                file_idx;
      } msg_t;
      
      rc_t read( handle_t h, msg_t* buf, unsigned buf_msg_cnt, unsigned& actual_msg_cnt_ref );

      rc_t test( const object_t* cfg );
      
    }
  }
} 
