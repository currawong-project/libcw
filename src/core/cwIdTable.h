
namespace cw
{

  namespace id_table
  {
    typedef handle<struct id_table_str> handle_t;

    rc_t create( handle_t& hRef, unsigned init_table_cnt );
    rc_t destroy( handle_t& hRef );

    unsigned    get_id( handle_t h, const char* label );
    const char* get_label( handle_t h, unsigned id );
    rc_t        get_label( handle_t h, unsigned id, const char*& label_ref );

    rc_t     create_global(unsigned init_table_cnt=1024);
    rc_t     destroy_global();
      
    unsigned get_id( const char* label );
    const char* get_label( unsigned id );
    rc_t        get_label( unsigned id, const char*& label_ref );
    
  }
}
