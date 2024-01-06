#ifndef cwB23Tree_h
#define cwB23Tree_h

namespace cw
{
  namespace b23
  {

    /*
      This is a binary tree implemented as a (2-3 tree)
      See: docs/2-3-trees.pdf
      or https://cs.wellesley.edu/~cs231/handouts/2-3-trees.pdf
     */
      
    
    template< typename K, typename V, K null_key >
    struct tree_str
    {

      typedef enum {
        kInvalidNodeTId,
        k1LeafTId,
        k2LeafTId,
        k2NodeTId,
        k3NodeTId
      } node_tid_t;
      
      typedef struct value_str
      {
        V value;
        struct value_str* link;
      } value_t;
      
      typedef struct key_value_str
      {
        K        key;     // Key for this k/v pair
        value_t* valueL;  // Linked list of values which share the same key.
        
        bool is_empty() const { return key==null_key; }
        bool is_not_empty() const  { return !is_empty(); }
        void set_empty() { key=null_key; valueL=nullptr; }
        
      } key_value_t;


      
      typedef struct node_str
      {

        unsigned nid;
        
        struct node_str* parent;
        struct node_str* l_link;
        struct node_str* m_link;
        struct node_str* h_link;

        // If kv1 is not empty then kv1.key is > kv0.key
        key_value_t kv0; // kv0 always contains a valid key-value pair
        key_value_t kv1; // kv1 is only valid if this is a 3 node

        unsigned key_value_count() const { return kv1.is_empty() ? 1 : 2; }

        const K&  min_key() const { return kv0.key; }
        const K&  max_key() const { return kv1.is_empty() ? kv0.key : kv1.key; }

        // Leaf nodes have no child pointers, but may have one or two key-value pairs.
        bool is_leaf() const   { return this->l_link == nullptr; }

        bool is_1_leaf() const { return is_leaf() && kv1.is_empty(); }
        bool is_2_leaf() const { return is_leaf() && kv1.is_not_empty(); }

        // 2 nodes have one key-value pair and valid l_link and h_link;
        bool is_2_node() const { return !this->is_leaf() && this->m_link == nullptr; }

        // 3 nodes have two key-value pairs and valid l,m,h links
        bool is_3_node() const { return !this->is_leaf() && this->m_link != nullptr; }

        node_tid_t type_id() const { return is_leaf() ? (is_1_leaf() ? k1LeafTId : k2LeafTId) : (is_2_node() ? k2NodeTId : k3NodeTId); }

        bool is_valid()
        {
          // if l_link is null then h_link and m_link are also null. if l_link is not null then neither is h_link.
          bool fl0 = l_link==nullptr ? (h_link==nullptr && m_link==nullptr) : h_link!=nullptr;
          
          // kv0 is never empty
          bool fl1 = kv0.is_not_empty() && kv0.valueL != nullptr;
          
          // m_link is null if kv1 is empty
          bool fl2 = kv1.is_empty() ? m_link==nullptr : m_link!=nullptr;

          return fl0 && fl1 && fl2;
        }

        
      } node_t;

      typedef struct node_block_str
      {
        struct node_block_str* link;
        node_t*                nodeA;
        unsigned               nodeN;
        unsigned               next_avail_node_idx; // index next empty slot      
      } node_block_t;

      typedef struct value_block_str
      {
        struct value_block_str* link;
        value_t*                valueA;
        unsigned                valueN;
        unsigned                next_avail_value_idx; // index of next empty slot
      } value_block_t;

      node_t*        _root = nullptr;
      node_block_t*  _beg_node_block = nullptr;   // First node in node block linked list
      node_block_t*  _end_node_block = nullptr;   // Last block in node block linked list (always partially empty)
      value_block_t* _beg_value_block = nullptr;  // First node in value block linked list
      value_block_t* _end_value_block = nullptr;  // Last block in value block linked list (always partially empty)
      node_t*        _free_node_list = nullptr;   // Linked list, through 'parent' of avail nodes. 
      unsigned       _nodes_per_block = 0;
      unsigned       _values_per_block = 0;
      unsigned       _nid = 0;

      const char* node_tid_to_label( node_tid_t tid )
      {
        switch(tid)
        {
        case kInvalidNodeTId: return "<inv>";
        case k1LeafTId: return "1L";
        case k2LeafTId: return "2L";
        case k2NodeTId: return "2N";          
        case k3NodeTId: return "3N";
        }
        return "<unk>";
      }

      
      // Return the node closest to the given key.
      // 
      node_t* key_to_node( K key )
      {
        node_t* n = _root;
        while(n != nullptr)
        {
          if( n->kv0.key == key )
            break;

          if( n->kv1.is_not_empty() and n->kv1.key == key )
            break;

          if( key < n->kv0.key )
          {
            n = n->l_link;
          }
          else
          {
            if(  key > (n->kv1.is_not_empty() ? n->kv1.key : n->kv0.key) )
              n = n->h_link;
            else
              n = n->m_link;
          }
        }

        return n;
      }
      
      node_block_t*  _alloc_node_block( unsigned nodes_per_block )
      {
        node_block_t* b = mem::allocZ<node_block_t>();
        
        b->nodeA   = mem::allocZ<node_t>( nodes_per_block );
        b->nodeN   = nodes_per_block;

        if( _beg_node_block == nullptr )
          _beg_node_block = b;
        else
          _end_node_block->link = b;
      
        _end_node_block = b;

        return b;
      }

      value_block_t*  _alloc_value_block( unsigned values_per_block )
      {
        value_block_t* b = mem::allocZ<value_block_t>();
        
        b->valueA   = mem::allocZ<value_t>( values_per_block );
        b->valueN   = values_per_block;

        if( _beg_value_block == nullptr )
          _beg_value_block = b;
        else
          _end_value_block->link = b;
      
        _end_value_block = b;

        return b;
      }

      void _alloc_value( key_value_t& kv, V new_value )
      {
        if( _end_value_block==nullptr || _end_value_block->next_avail_value_idx >= _end_value_block->valueN )
          _alloc_value_block(_values_per_block);

        assert( _end_value_block!= nullptr && _end_value_block->next_avail_value_idx < _end_value_block->valueN );

        value_t* v = _end_value_block->valueA + _end_value_block->next_avail_value_idx++;

        v->value = new_value;
        v->link = kv.valueL;
        kv.valueL = v;
        
      }

      // Initialize a kv with a new key, value pair
      void _init_key_value( key_value_t& kv, K key, V value )
      {
        kv.key = key;
        _alloc_value(kv,value);
      }

      // Initialize a kv from an existing kv pair
      void _move_key_value( key_value_t& lhs_kv, key_value_t& rhs_kv )
      {
        lhs_kv = rhs_kv;
        rhs_kv.set_empty();
      }

      node_t* _alloc_node( node_t* parent )
      {
        node_t* n = nullptr;
        
        if( _free_node_list != nullptr )
        {
          n = _free_node_list;
          memset(n,0,sizeof(*n));
          _free_node_list = _free_node_list->parent;          
        }
        else
        {
          // if the current node block has no available nodes then create a new node block
          if( _end_node_block==nullptr || _end_node_block->next_avail_node_idx >= _end_node_block->nodeN )
            _alloc_node_block(_nodes_per_block);

          // a node block with available nodes must now exist 
          assert( _end_node_block!= nullptr && _end_node_block->next_avail_node_idx < _end_node_block->nodeN );
          
          // get the next available node
          n = _end_node_block->nodeA + _end_node_block->next_avail_node_idx++;
        }
        
        n->nid    = _nid++;
        n->parent = parent;
        n->kv0.key = null_key;
        n->kv1.key = null_key;
        return n;
      }

      // allocate a new node with a new key / value pair
      node_t* _alloc_node( node_t* parent, K new_key, V new_value )
      {
        node_t* n = _alloc_node(parent);
        
        // all new nodes have a valid k/v pair in kv0
        _init_key_value( n->kv0, new_key, new_value );
        
        return n;
      }

      // allocate a new node with an existing k/v apir.
      node_t* _alloc_node( node_t* parent, key_value_t& kv )
      {
        node_t* n = _alloc_node(parent);
        _move_key_value( n->kv0, kv );
        return n;
      }

      void _free_node( node_t* node )
      {
        // track free nodes by forming a list using the 'parent' pointer
        node->parent = _free_node_list;
        _free_node_list = node;
      }
      
      rc_t create( unsigned nodes_per_block )
      {
        rc_t rc  = kOkRC;

        _nodes_per_block = nodes_per_block;
        _values_per_block= nodes_per_block;

        _alloc_node_block(nodes_per_block);
        _alloc_value_block(nodes_per_block);
        
        return rc;
      }      

      void destroy()
      {
        value_block_t* vb = _beg_value_block;
        while( vb!=nullptr )
        {
          value_block_t* vb0 = vb->link;
          mem::release(vb->valueA);
          mem::release(vb);
          vb=vb0;
        }

        node_block_t* nb = _beg_node_block;
        while( nb !=nullptr )
        {
          node_block_t* nb0 = nb->link;
          mem::release(nb->nodeA);
          mem::release(nb);
          nb=nb0;
        }
      }

      void _insert_into_1_leaf(node_t* n, K key, V value )
      {
        if( key > n->kv0.key )
          _init_key_value(n->kv1,key,value);
        else
        {
          _move_key_value(n->kv1,n->kv0);
          _init_key_value(n->kv0,key,value);
        }
      }


      node_t* _2_leaf_to_2_node_sub_tree( node_t* n, K key, V value )
      {
        assert( n->is_2_leaf() );
        
        if( key < n->kv0.key )
        {
          n->l_link = _alloc_node(n,key,value);
          n->h_link = _alloc_node(n,n->kv1);            
        }
        else
        {
          if( key > n->kv1.key )
          {
            n->l_link = _alloc_node(n,n->kv0);
            n->h_link = _alloc_node(n,key,value);
            _move_key_value(n->kv0,n->kv1);
          }
          else
          {
            n->l_link = _alloc_node(n,n->kv0);
            n->h_link = _alloc_node(n,n->kv1);
            _init_key_value(n->kv0,key,value);
          }
        }

        assert(n->is_2_node());
        return n;
      }

      void _link_to_parent_l( node_t* parent, node_t* child )
      {
        parent->l_link = child;
        child->parent = parent;
      }
      
      void _link_to_parent_m( node_t* parent, node_t* child )
      {
        parent->m_link = child;
        child->parent = parent;
      }
      void _link_to_parent_h( node_t* parent, node_t* child )
      {
        parent->h_link = child;
        child->parent = parent;
      }

      
      // convert 'n', a 2-node, into a 3-node by absorbing it's 2-node child 
      void _2_node_to_3_node( node_t* n, node_t* child )
      {
        assert( n->is_2_node() );
        
        // if child is the l subtree
        if( child == n->l_link )
        {
          _move_key_value(n->kv1,n->kv0);
          _move_key_value(n->kv0,child->kv0);

          _link_to_parent_l(n,child->l_link);
          _link_to_parent_m(n,child->h_link);
          
        }
        else  // child must be the h subtree
        {
          assert( child == n->h_link );

          _move_key_value(n->kv1,child->kv0);

          _link_to_parent_m(n,child->l_link);
          _link_to_parent_h(n,child->h_link);
        }

        _free_node(child);

        assert( n->is_3_node() );
      }

      node_t* _2node_from_parts( node_t* parent, key_value_t& kv, node_t* l_subtree, node_t* h_subtree )
      {
          node_t* h_node = _alloc_node(parent,kv);
          _link_to_parent_l(h_node,l_subtree);
          _link_to_parent_h(h_node,h_subtree);
          return h_node;
      }

      // Convert a 3-node to a balanced 2-node.
      // 'child' is a balanced sub-trees of 'n' 
      void _3_node_to_balanced_2_node( node_t* n, node_t* child )
      {
        assert( n->is_3_node() );
        assert( child->is_2_node() );

        // if the child is on the l-subtree
        if( child == n->l_link )
        {
          // make l-key the central node and the h-key the balanced h-subtree
          n->h_link = _2node_from_parts(n,n->kv1,n->m_link,n->h_link);
        }
        else
        {
          if( child == n->h_link )
          {
            // make h-key the central node and l-key the balanced l-subtree
            n->l_link = _2node_from_parts(n,n->kv0,n->l_link,n->m_link);
            _move_key_value(n->kv0,n->kv1);            
          }
          else
          {
            // make m-subtree he central node and l-key the balanced l-subtree and h-key the balanced h-subtree
            assert( child == n->m_link );
            n->l_link = _2node_from_parts(n,n->kv0,n->l_link,child->l_link);
            n->h_link = _2node_from_parts(n,n->kv1,child->h_link,n->h_link);
            _move_key_value(n->kv0,child->kv0);
          }
        }

        // n is now a 2-node - remove the old m_link
        n->m_link = nullptr;
        
        assert( n->is_2_node() );
        assert( n->l_link->is_2_node() );
        assert( n->h_link->is_2_node() );
      }
      
      void _insert_up( node_t* n, node_t* sub_tree )
      {
        while(1)
        {
            // if n is null then the root was already processed and we are done
          if( n == nullptr )
            break;
          
          if( n->is_2_node() )
          {
            // if n is a 2-node the sub-tree is absorved ...
            _2_node_to_3_node(n,sub_tree); 
            break; // .. and we are done
          }
          else // n is a 3-node
          {
            // only 2-nodes and 3-nodes can be accessed when going up the tree
            assert( n->is_3_node() );
            
            // create a balanced 2-node from the 3-node + sub-tree
            _3_node_to_balanced_2_node(n,sub_tree);

            // the tree may now be imbalanced to continue upward
            sub_tree = n;
            n = n->parent;
          }
        }
      }
      
      void _insert_down( node_t* n, K key, V value )
      {
        while(1)
        {
          if( key == n->kv0.key )
          {
            _alloc_value(n->kv0,value);
            return;
          }

          if( n->kv1.is_not_empty() && key == n->kv1.key )
          { 
            _alloc_value(n->kv1,value);
            return;
          }
          
          switch( n->type_id() )
          {
          case k1LeafTId:
            _insert_into_1_leaf(n,key,value);
            return; // the new k/v was absorbed - we're done.
            
          case k2LeafTId:
            if( key == 10 )
            {
              printf("break");
            }
            _insert_up( n->parent, _2_leaf_to_2_node_sub_tree(n, key, value ));
            return; // the new k/v inserted on the upward path
            
          case k2NodeTId:
            n = key < n->kv0.key ? n->l_link : n->h_link;
            break;
            
          case k3NodeTId:
            n = key < n->kv0.key ? n->l_link : (key < n->kv1.key ? n->m_link : n->h_link);
            break;
            
          default:
            assert(0);
          }
          
        }
      }
      
      void insert( K key, V value )
      {
        if( _root == nullptr )
          _root = _alloc_node(nullptr,key,value);
        else
          _insert_down(_root,key,value);        
      }

      void delete( K key )
      {
        // if key is found on internal node - replace with in-order successor.
        // if in-order successor is on a non-leaf node continue replacing
        // with in-order successor until the replacement leaves a hole
        // in a leaf node.
        // If the terminal node with the hole is a 2-leaf then change it to a 1-leaf : DONE
        // if the terminal node is a 3-leaf then
      }

      

      
      void _print( const node_t* n, unsigned level )
      {
        if( n->l_link != nullptr )
          _print(n->l_link,level + 1);

        unsigned pnid = n->parent==nullptr ? 666 : n->parent->nid;
        printf("%i k0:%i %s id:%i par:%i\n",level,n->kv0.key,node_tid_to_label(n->type_id()),n->nid,pnid);
        
        if( n->m_link != nullptr )
          _print(n->m_link,level+1);
        
        if( n->kv1.is_not_empty() )
          printf("%i k1:%i %s id:%i par:%i\n",level,n->kv1.key,node_tid_to_label(n->type_id()),n->nid,pnid);
            
        if( n->h_link != nullptr )
          _print(n->h_link,level+1);
          
                
      }
      
      void print(const node_t* n = nullptr)
      {
        unsigned level = 0;
        
        if( n == nullptr )
          n = _root;
        _print(n,level);
        printf("done\n");
        
      }
      
    };

    rc_t test( const object_t* cfg );
  }

}


    
#endif
