#ifndef cwAutoRange_h
#define cwAutoRange_h


namespace cw
{
  namespace auto_range
  {
    /*
      Track the incoming values to determine a stable range
      and then scale to the output range.

      1. Use input history to calculate the deviation from the mean.
      2. Use the deviation to determine the range min/max.
      3. Form the output value based on input deviation range and output range.
      4. Apply output filter.
      
      
     */
    
    typedef handle<struct auto_range_str> handle_t;

    /* Config object syntax:
       
      {
        history_cnt:<>
        varL: [
         { label:<>, .... bool_threshold:<> }
        ]
      }
     */

    rc_t create( handle_t& hRef, const char* fname );
    rc_t create( handle_t& hRef, const object_t* cfg );    
    rc_t create( handle_t& hRef, unsigned variable_cnt, unsigned history_cnt );
    
    rc_t destroy( handle_t& hRef );

    rc_t reset( handle_t h );

    typedef struct
    {
      const char*   label;
      double        default_out_value; // output value to report if no input values are available.
      double        dev_mult;          // how many std dev's define the unit range 0=default=1.0
      double        min_out_value;     // min possible output value
      double        max_out_value;     // max possible output value
      double        out_filter_coeff;  // 0=no filter 
      double        bool_threshold;    // 0=default=0.5
    } cfg_t;

    rc_t register_variable( handle_t h, const cfg_t& cfg, unsigned variable_id_ref );

    unsigned variable_label_to_id( handle_t h, const char* label );

    // Return the count of registered variables.
    unsigned variable_count( handle_t h );
    
    // Note that index is not a variable id, as returned by register_variable but rather an index
    // in the range 0 to variable_count()-1. Returns null if index >= variable_count().
    const cfg_t* index_to_cfg( handle_t h, unsigned index );

    // Given an id return the variable's 
    const cfg_t* id_to_cfg( handle_t h, unsigned variable_id );

    rc_t update_variable( handle_t h, unsigned variable_id, const unsigned& value );
    rc_t update_variable( handle_t h, unsigned variable_id, const double&   value );
    

    inline rc_t update_variable( handle_t h, double sec ) { return kOkRC; };

    template< typename T, typename... ARGS >
    rc_t update_variable( handle_t h, double sec, unsigned variable_id, const T& value, ARGS&&... args )
    {
      rc_t rc = kOkRC;
      
      if((rc = update_variable(h,sec,variable_id,value)) != kOkRC )
        return rc;

      return update_variable(h,sec,std::forward<ARGS>(args)...);
    }

    rc_t get_value( handle_t h, double sec, unsigned variable_id, bool& value_ref );
    rc_t get_value( handle_t h, double sec, unsigned variable_id, int& value_ref );
    rc_t get_value( handle_t h, double sec, unsigned variable_id, unsigned& value_ref );
    rc_t get_value( handle_t h, double sec, unsigned variable_id, double&   value_ref );

    inline rc_t get_value(handle_t h, double sec ) { return kOkRC; }
    
    template< typename T, typename... ARGS >
    rc_t get_value( handle_t h, double sec, unsigned variable_id, T& value_ref, ARGS&&... args )
    {
      rc_t rc = kOkRC;
      if((rc = get_value(h,sec,variable_id,value_ref)) != kOkRC )
        return rc;
      return get_value(h,sec,std::forward<ARGS>(args)...);
    }

    void report( handle_t h );
    
  }
}


#endif
