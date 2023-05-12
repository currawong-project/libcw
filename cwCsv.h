#ifndef cwCsv_h
#define cwCsv_h

namespace cw
{
  namespace csv
  {
    typedef handle<struct csv_str> handle_t;

    rc_t create( handle_t& hRef, const char* fname, const char** titleA=nullptr, unsigned titleN=0 );
    
    rc_t destroy(handle_t& hRef );

    rc_t line_count( handle_t h, unsigned& lineCntRef );

    unsigned title_col_index( handle_t h, const char* title );

    rc_t rewind( handle_t h );
    
    rc_t next_line( handle_t h );

    // line index (first line==0) of the line currently being parsed.
    unsigned cur_line_index( handle_t h );

    rc_t field_char_count( handle_t h, unsigned colIdx, unsigned& charCntRef );

    rc_t parse_field( handle_t h, unsigned colIdx, unsigned& valRef );
    rc_t parse_field( handle_t h, unsigned colIdx, int& valRef );
    rc_t parse_field( handle_t h, unsigned colIdx, double& valRef );
    rc_t parse_field( handle_t h, unsigned colIdx, const char*& valRef );

    rc_t parse_field( handle_t h, const char* colLabel, unsigned& valRef );
    rc_t parse_field( handle_t h, const char* colLabel, int& valRef );
    rc_t parse_field( handle_t h, const char* colLabel, double& valRef );
    rc_t parse_field( handle_t h, const char* colLabel, const char*& valRef );

    inline rc_t _getv(handle_t) { return kOkRC; } 

    // getv("label0",v0,"label1",v1, ... )
    template< typename T0, typename T1, typename... ARGS >
      rc_t _getv( handle_t h, T0 label, T1& valRef, ARGS&&... args )
    {
      rc_t rc = parse_field(h,label,valRef);

      // if no error occurred ....
      if( rc == kOkRC )
        rc =  _getv(h,std::forward<ARGS>(args)...); // ... recurse to find next label/value pair
      else
        rc = cwLogError(rc,"CSV parse failed for column label:'%s' on line index:%i.",cwStringNullGuard(label),cur_line_index(h));

      return rc;
    }

    // getv("label0",v0,"label1",v1, ... )
    template< typename T0, typename T1, typename... ARGS >
    rc_t getv( handle_t h, T0 label, T1& valRef, ARGS&&... args )
    { return _getv(h,label,valRef,args...); }    

    rc_t test( const object_t* args );
  }
}

#endif
