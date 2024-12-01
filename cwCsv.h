//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwCsv_h
#define cwCsv_h

namespace cw
{
  namespace csv
  {
    typedef handle<struct csv_str> handle_t;

    // The first line of the CSV is expected to hold the column titles.
    // If titlesA and titleN are valid then these will be verified to exist when the CSV file is opened.
    rc_t create( handle_t& hRef, const char* fname, const char** titleA=nullptr, unsigned titleN=0 );
    
    rc_t destroy(handle_t& hRef );

    // Count of lines in the CSV including the title line.
    // Subtract 1 to get the count of data lines.
    rc_t line_count( handle_t h, unsigned& lineCntRef );

    // Count of columns in the first row (title row).
    unsigned col_count( handle_t h );
    
    const char* col_title( handle_t h, unsigned idx );
    unsigned title_col_index( handle_t h, const char* title );
    bool has_field( handle_t h, const char* title );

    // Reset the CSV to make the title line current.
    // The next call to 'next_line()' will make the first data row current.
    rc_t rewind( handle_t h );

    // Make the next row current. The 'getv()' and parse_???()' functions
    // operate on the current row.
    // This function return kEofRC when it increments past the last line in the file.
    rc_t next_line( handle_t h );

    // line index (first line==0) of the line currently bei[ng parsed.
    unsigned cur_line_index( handle_t h );

    // Return the count of characters in the field identified by 'colIdx'.
    rc_t field_char_count( handle_t h, unsigned colIdx, unsigned& charCntRef );

    rc_t parse_field( handle_t h, unsigned colIdx, uint8_t& valRef );
    rc_t parse_field( handle_t h, unsigned colIdx, unsigned& valRef );
    rc_t parse_field( handle_t h, unsigned colIdx, int& valRef );
    rc_t parse_field( handle_t h, unsigned colIdx, double& valRef );
    rc_t parse_field( handle_t h, unsigned colIdx, bool& valRef );
    
    
    // The returned pointer is a pointer into an internal 'line' buffer.
    // The reference is therefore only valid until the next call to next_line().
    rc_t parse_field( handle_t h, unsigned colIdx, const char*& valRef );

    rc_t parse_field( handle_t h, const char* colLabel, uint8_t& valRef );
    rc_t parse_field( handle_t h, const char* colLabel, unsigned& valRef );
    rc_t parse_field( handle_t h, const char* colLabel, int& valRef );
    rc_t parse_field( handle_t h, const char* colLabel, double& valRef );
    rc_t parse_field( handle_t h, const char* colLabel, bool& valRef );
    
    // The returned pointer is a pointer into an internal 'line' buffer.
    // The reference is therefore only valid until the next call to next_line().
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
