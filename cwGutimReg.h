#ifndef cwGutimReg_h
#define cwGutimReg_h

namespace cw
{
  namespace gutim
  {
    namespace reg
    {
      typedef handle<struct reg_str> handle_t;

      typedef struct file_str
      {
        const char* player_name;
        const char* take_label;
        const char* path; 
        const char* midi_fname;
        bool        skip_score_follow_fl;
        unsigned    session_number;
        unsigned    take_number;
        unsigned    beg_loc;
        unsigned    end_loc;
      } file_t;
      
      
      rc_t create(  handle_t& hRef, const char* fname );
      rc_t destroy( handle_t& hRef );

      unsigned file_count(  handle_t h );
      file_t   file_record( handle_t h, unsigned file_idx );

      void report( handle_t h );
      
      
    }
  }
}

#endif
