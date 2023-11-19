namespace cw
{
  namespace perf_meas
  {
    
    typedef handle<struct perf_meas_str> handle_t;

    enum {
      kDynValIdx,
      kEvenValIdx,
      kTempoValIdx,
      kMatchCostValIdx,
      kValCnt
    };
    
    typedef struct result_str
    {
      unsigned      loc;          // current location (which may be greater than sectionLoc)
      unsigned      sectionLoc;   // location of the first event in this section
      const char*   sectionLabel; // label of triggered section
      const double* valueA;       // valueA[valueN] se k???ValIdx to extract values by type
      unsigned      valueN;       // Count of elements in valueA[]      
    } result_t;

    typedef struct params_str
    {
      bool print_rt_events_fl;
    } params_t;

    rc_t create(  handle_t& hRef, sfscore::handle_t scoreH, const params_t& params );
    rc_t destroy( handle_t& hRef );

    rc_t reset( handle_t h, unsigned init_locId );

    // resultRef.loc == kInvvalidIdx and resultRef.sectionLoc is set to kInvalidIdx
    // and the other fields are zeroed if this event was not associated with a 'section' boundary.
    rc_t exec( handle_t h, const sfscore::event_t* event, result_t& resultRef );

    void report( handle_t h );
    
    rc_t write_result_csv( handle_t h, const char* out_fname );

    rc_t write_result_json( handle_t h, const char* player_name, const char* perf_date, unsigned perf_take_numb, const char* out_fname );
    
    
  }
}
