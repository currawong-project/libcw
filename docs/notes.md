Miscellaneous Notes on flow and libcw design and implementation


# Flow Instance Creation (Original Implementation):

0. Parse the 'in' list and create any 'mult' variables whose 
'in-var' contains an integer or underscore suffix. See
"'in' List Syntax and Semantics" below.

1. Create all vars from the class description, that were not
already instantiated during 'in' list processing, and set their
initial value to the default value given in the class.  chIdx=kAnyChIdx.

Notes:
- All vars must be included in the class description.
- All vars have a 'kAnyChIdx' instantiation. The kAnyChIdx variable serves two purposes:
  + Setting the value of  kAnyChIdx automatically broadcasts the value to all other channels.
  + kAnyChIdx acts as a template when variables are created by 'channelization'.
    This allows the network designer to set the value of the kAnyIdx variable
	and have that become the default value for all subsequent variables
	which are created without an explicit value.  (Note that his currently
	works for variables created from within `proc_create()` (i.e.
	where `sfx_id` == kBaseSfxId, but it doesn't work for mult 
	variables that are automatically created via `var_register()`
	because `var_register()` does not have a value to assign to the 
	kAnyChIdx instance. In this case the variable get assigned
	the class default value.  The way around this is to explicitely
	set the mult variable value in the 'in' stmt or the 'args' stmt.)
	

2. Apply the preset records from the class description according to the
'presets' list given in the instance definition. 

If the variable values are given as a scalar then the existing
variable is simply given a new value.

If the variable values are given as a list then new variables 
records will be created with explicit channels based on the
index of the value in the list. This is referred
to as 'channelizing' the variable because the variable will then
be represented by multiple physical variable records - one for each channel.
This means that all variables will have their initial record, with the chIdx set to 'any',
and then they may also have further variable records for each explicit
channel number. The complete list of channelized variable record 
is kept, in channel order, using the 'ch_link' links with the base of the list
on the 'any' record.

3. Apply the variable values defined in a instance 'args' record.

The application of the args record proceeds exactly the same as
applying a 'class' preset. If the variable value is presented in a
list then the value is assigned to a specific channel. If the channel
already exists then the value is simply replaced. If the channel does
not exist then the variable is 'channelized'.

4. The varaibles listed in the 'in' list of the instance cfg.
are connected to their source variables.

5. The custom class constructor is run for the instance.

Within the custom class constructor the variables to be used by the
instance are 'registered' via var_register().  Registering
a variable allows the variable to be assigned a constant
id with which the instance can access the variable very efficiently.

If the channel associated with the registered variable does not
yet exist for the variable then a channelized variable is first created
before registering the variable.

6. The internal variable id map is created to implement  fast
access to registered variables.


# Notes on 'poly' and 'mult':

The 'in' statement is formed by a list of _Connect Expressions_ :

`<input_var>:<source_inst.source_var>`

There are three forms of connect expressions:

1. Simple Connect Expression: Both the input and source labels
identify vars in the input and source instance.

2. Manual Mult Connect Expression: The input identifer ends with an
integer.  This expression indicates that an input var will be
instantiated and connected to the source var.  The integer indicates
the suffix (sfx) id of the input var. e.g. `in0:osc.out`,`in1:filt.out`.
      
3. PolyMult Connect Expression: The source identifier has an
underscore suffix.  This form indicates that there will one instance of
this var for each poly instance that the source var instances is
contained by. e.g. `in:osc_.out` If `osc` is contained by an order 3
poly then statement will create and connect three instances of `in` -
`in0:osc0.out`,`in1:osc1.out` and `in2:osc2.out`.

Notes:
- For an input variable to be used in either of the 'Manual' or 'PolyMult' 
forms the var class desc must have the 'mult' attribute.

- If any var has an integer suffix then this is converted to it's sfx id.

- If the input var of a poly mult expression has an integer suffix then this is taken to be the
base sfx id for that poly connection.  Other connections in the same statement will be
incremented from that base value.  e.g `in3:osc_.out` becomes
`in3:osc0.out`,`in4:osc1.out` and `in5:osc2.out`.

- The first and last poly source instance can be indicated by specifying a 
begin poly index and count before and after the source index underscore:
e.g. `in:osc3_3.out` becomes: `in0:osc3.out`,`in1:osc4.out` and `in2:osc5.out`.

- A similar scheme can be used to indicate particular source instance vars:
`in:osc.out1_2` becomes `in0:osc.out1`,`in1:osc.out2`

- It is a compile time error to have more than one input variable with the same sfx id.

'in' List Syntax and Semantics:
===============================

Syntax:
-------
The 'in' list has the follow syntax:
`in: { in-stmt* }`
`in-stmt`     -> `in_expr`":" `src_expr`
`in-expr`     -> `in-proc-id`".`in-var-id`
`src-expr`    -> `src-proc-id`"."`src-var-id`
`in-var-id`   -> `var-id`
`src-proc-id` -> `var-id`
`src-var-id`  -> `var-id`
`var-id`      -> `label` { `label-sfx` }
`label-sfx`   -> { `pri-int`} {{"_"} `sec-int` }
`pri-int`     -> int
`sec-int`     -> int


Semantics:
----------

### `in-proc-id`

- The `in-proc-id` is only used when the in-stmt
is iterating over the in-proc sfx-id. 
This precludes iterating over the in-var, as discussed below.

In this case the only useful option is to set the 'var-id` to `_`
as the in-proc is taken as the the proc which the
in-stmt belongs to.

The iterating source and/or var sfx-id are then set
to the current proc sfx-id + source `pri-int`.


### `in-var-id`

- The `label` part of the `in-var-id`  must match to a 
var description in the input proc class description.

- If no `label-sfx` is given then no special action
need by taken at var creation time. This var will be
created by default and later connected to the source inst/var.


- (0) If the "_" is given:
    + This is an "iterating" in-stmt where multiple 
	  input vars will be created and connected.
	
	+ If no `pri-int` is given  then the `pri-int` defaults to 0.
	
	+ If the `pri-int` is given then it indicates that 
      an instance of this var should be created where
      the `pri-int` becomes the sfx-id of the var instance.

	+ If `sec-int` is given then it gives the 
      count of input vars which will be created. The
	  sfx-id of each new input var begins with `pri-int` 
	  and increments for each new var.

    + (1) If no `sec-int` is given then the `sec-int` is implied by the count
	  of source variables indicated in the `src-expr`.


- If "_" is not given:
    + No `sec-int` can exist without a "_".
	
    + If a `pri-int` is given then a single
	  input var is created and the `pri-int` 
	  gives the sfx-id.  This single input
	  var is then connected to a single src var.
	  
	+ If no `pri-int` is given
	  then the default var is created
	  with kBaseSfxId and is connected
      to a single source var.


### `src-proc-id`

- The `label` part of the `src-proc-id` must match to a 
previously created proc instance in the current network.

- If a `label-sfx` is given then the `pri-int` gives
the sfx-id of the first proc inst to connect to.
If no `pri-int` is given then the first sfx-id 
defaults to 0. 


- If "_" is given:
	+ This is an "iterating" src-proc and therefore
	  the in-var must also be iterating. See (0)

	+ If a `sec-int` is given then this gives the count of
      connections across multiple proc instances with
	  sfx-id's beginnign with `pri-int`.  Note that if
	  `sec-int` is given then the `in-var-id` must be
	  iterating and NOT specify an iteration count,
	  as in (1) above.
	  
	+ If no `sec-int` is given then the 
	  `sec-int` defaults to the count of
	  available proc instances with the given `label` 
	  following the source proc inst `pri-int`.



- If "_" is not given then this is not an
  iterating proc inst. 
  
    + If the input var is iterating
      then it must specify the iteration count or
	  the `src-var-id` must be iterating.
	  
	+ If the `pri-int` is given then it specifies
	  the sfx-id of the src-proc
	  
	+ If the `pri-int` is not given 
	
	    - If the src-net is the same as the in-var net then
	      the sfx-id of the in-var proc is used as the src-proc sfx-id


### `src-var-id`

- The `label` part of the `in-var-id`  must match to a 
var description in the source proc class descriptions.

- If a `label-sfx` is given then the `pri-int` gives
the sfx-id of the first source var to connect to
on the source proc instance. If no `pri-int` is
given then the first sfx-id defaults to 0.

- If a "_" is given:
    + This is an "iterating"
	  source var and therefore the input var
	  must specifiy an iterating connection and
	  the source proc inst must not specify an iterating 
	  connection. See (0) above.

	+ If a `sec-int` is given then this gives the count of
      connections across multiple source vars with
	  sfx-id's beginnign with `pri-int`.  Note that if
	  `sec-int` is given then the `in-var-id` must be
	  iterating and NOT specify an iteration count,
	  as in (1) above.


	+ If `sec-int` is not given
	  then the `sec-int` defaults to the count of
	  available source vars with the given `label` 
	  following the source var `pri-int`.

- If "_" is not given then this is not an
  iterating source var. If the input var is iterating
  then it must specify the iteration count or
  the `src-proc-id` must be iterating.


### Notes:

- If the `in-var-id` is iterating but neither `src-proc-id`
or `src-var-id` are iterating then the `in-var-id` must
specify the iteration count and the connection will be
made to exactly one source var on the source proc inst.

- If `in-var-id` is iterating then the iterations count
must come from exactly one place:
    + the input var `sec-int`
	+ the source proc `sec-int`
	+ the source var `sec-int`
	
This means that only one literal iter count can be 
given per `in-stmt`.  It is a syntax error if
more than one literal iter counts are given.

- Use cases
    + connect one input to one source
	+ connect multiple inputs to the same var on multiple procs
	+ connect multiple inputs to multiple vars on one proc
	+ connect multiple inputs to one var on one proc


### in-stmt Examples:

`in:sproc.svar`   Connect the local variable `in` to the source variable `sproc.svar`.

`in0:sproc.svar`  Create variables `in0` and connect to `sproc.svar`.

`in_2:sproc.svar` Create variables `in0` and `in1` and connect both new variables to `sproc.svar`.

`in_:sproc.svar0_2` Create variables `in0` and `in1` and connect them to `sproc.svar0` and `sproc.svar1`.

`in3_3:sproc.svar` Create variables `in3`,`in4` and `in5` and connect them all to `sproc.svar`.

`in_:sproc.svar1_2` Create variables `in0`,`in1` and connect them to `sproc.svar1` and `sproc.svar2`.

`in1_2:sproc.svar3_` Create variables `in1`,`in2` and connect them to `sproc.svar3` and `sproc.svar4`.

`in_:sproc.svar_` Create vars `in0 ... n-1` where `n` is count of vars on `sproc` with the label `svar`.
`n` is called the 'mult-count' of `svar`.  The new variables `in0 ... n-1` are also connected to `sproc.svar0 ... n-1`.

`in_:sproc_.svar` Create vars `in0 ... n` where `n` is determineed by the count of procs named `sproc`.
`n` is called the 'poly-count' of `sproc`. The new variables `in0 ... n-1` are also connected to `sproc.svar0 ... n-1`

If an underscore precedes the in-var then this implies that the connection is being
made from a poly context. 

`foo : { ... in:{ _.in:sproc.svar_ } ... } ` This example shows an excerpt from the network 
definition of proc `foo` which is assumed to be a poly proc (there are multiple procs named 'foo' in this network).
This connection iterates across the procs  `foo:0 ... foo:n-1` connecting the 
the local variable 'in' to `sproc.svar0 ... n-1`. Where `n` is the poly count of `foo`.

`foo : { ... in:{ 1_3.in:sproc.svar_ } ... }` Connect `foo:1-in:0` to `sproc:svar0` and `foo:2-in:0` to `sproc:svar1`.

`foo : { ... in:{ 1_3.in:sproc_.svar } ... }` Connect `foo:1-in:0` to `sproc0:svar0` and `foo:2-in:0` to `sproc1:svar`.

#### in-stmt Anti-Examples

`in_:sproc_.svar_` This is illegal because there is no way to determine how many `in` variables should be created.

`in:sproc.svar_` This is illegal because it suggests that multiple sources should be connected to a single input variable.

`in:sproc_.svar` This is illegal because it suggests that multiple sources should be connected to a single input variable.

`_.in_:sproc.svar` This is illegal because it suggests simultaneously iterating across both the local proc and var.
This would be possible if there was a way to separate how the two separate iterations should be distributed 
to the source. To make this legal would require an additional special character to show how to apply the poly
iteration and/or var iteration to the source. (e.g. `_.in_:sproc*.svar_`) 

### out-stmt Examples:

`out:iproc.ivar` Connect the local source variable `out` to the input variable `iproc:ivar`.

`out:iproc.ivar_` Connect the local source variable `out` to the input variables `iproc:ivar0` and `iproc:ivar1`.

`out_:iproc.ivar_` Connect the local souce variables `out0 ... out n-1` to the input variables `iproc:ivar0 ... iproc:ivar n-1`
where `n` is the mult count of the `out`.

`out_:iproc_.ivar` Connect the local souce variables `out0 ... out n-1` to the input variables `iproc0:ivar ... iproc n-1:ivar`
where `n` is the mult count of the `out`.


`_.out:iproc.ivar_` Connect the local source variables `foo0:out`, `foo n-1:out` to the input variables `iproc:ivar0`, `iproc:ivar n-1`.
where `n` is the poly count of `foo`.



Var Updates and Preset Application
==================================

Variable addresses are formed from the following parameters:
`(<proc_label><proc_label_sfx_id>)*,var_label,var_label_sfx_id, ch_idx`

In the cases of poly procs (procs with public internal networks) it
may not always be possible to know the `<proc_label_sfx_id>` without
asking for it at runtime.  For example for the cross-fader control the
application must ask for the `<proc_label_sfx_id>` current or next
poly channel depending on which one it is targetting.

It is notable that any proc with an internal network has
to solve this problem.  The problem is especially acute
for proc's which change the 'current' poly channel at runtime.

The alternative is to include the concept of special values 
in the address (e.g. kInvalidIdx means the application isn't
sure and the network should decide how to resolve the address)
The problem with this is that the information
to make that decision may require more information than
just a simple 'special value' can encode. It also means
complicating the var set/get pipeline with 'escape' routines.

There are at least two known use cases which need to address 
this issue:
1. The cross-fader: The application may wish to address
  updates to the current or next poly channel but this
  channel can't be determined until runtime.

- The application asks for the current or next `proc_label_sfx_id`
  at runtime depending on what its interested in doing, 
  and sets the update address accordingly.
  
  
- Two interface objects are setup as sources for the `xfade_ctl` 
  object.  The address of each of these objects can be 
  determined prior to runtime. The application then simply addresses
  the object corresponding to method (direct vs deferred) it requires.
  This solution is particularly appealing because it means that
  presets may be completely resolved to their potential 
  target procs (there would be up to 'poly-count' potential targets)
  prior to runtime.
  
  As it stands now the problem with this approach is that it
  does not allow for the message to be resolved to its final
  destination. If the message is addressed to a proxy proc
  then that proxy must mimic all the vars on the object which
  it is providing an interface for. (This is actually possible
  and may be a viable solution???) 
    
  One solution to this is to create a data type which is an
  address/value packet.  The packet would then be directed
  to a router which would in turn use the value to forward
  the packet to the next destination.  Each router that the
  packet passed through would strip off a value and
  pass along the message. This is sensible since the 'value'
  associated with a router is in fact another address.
  
2. The polyphonic sampler: 

- How can voices be addressed once they are started?
  + A given note is started - how do we later address that note to turn it off?
    Answer: MIDI pitch and channel - only one note may be sounding on a given MIDI pitch and channel at any one time.
	
    - Extending ths idea to the xfader: There are two channels: current and deferred,
	but which are redirected to point to 2 of the 3  physical channels .... this would
	require the idea of 'redirected' networks, i.e. networks whose proc lists were
	really pointers to the physical procs.
	  - sd_poly maintains the physical networks as it is currently implemnted.
	  - xfade_ctl maintains the redirected networks - requests for proc/var addresses 
	    on the redirected networks will naturally resolve to physical networks. 

	  - Required modifications:
	      + variable getters and setters must use a variable args scheme specify the var address:
		    `(proc-name,proc-sfx-id)*, var-name,var-sfx-id`
			Example: `xfad_ctl,0,pva,1,wnd_len,0,0`
	            - The first 0 is known because there is only one `xfad_ctl`.
				- The 1 indicates the 'deferred' channel.				
				- The second 0 is known because there is only one `wnd_len` per `pva`.
				- The third 0 indicates the channel index of the var.
			
		  + the address resolver must then recognize how to follow internal networks
		      
		  + Networks must be maintained as lists of pointers to procs
		    rather than a linked list of physical pointers.
			
		  + `xfade_ctl` must be instantiated after `sd_poly` and be able
			  to access the internal network built by `sd_poly`. 
		  
	
Generalizing the Addressing
---------------------------
Change the set/get interface to include a list of (proc-label,proc-sfx-id) 
to determine the address of the var. 

Note that this still requires knowing the final address in advance.
In general a router will not know how to resolve a msg to the 
next destination without having a final address.
In otherwords setting 'proc-sfx-id' to kInvalidId is not
resolvable without more information.

   
   

### TODO:


Proc instantiation
------------------
Prior to executing the custom constructor the values are assigned to the 
variables as follows:
1. Default value as defined by the class are applied when the variable is created.
2. The proc instance 'preset' class preset is applied.
3. The proc instance 'args' values are applied.

During this stage of processing preset values may be given for
variables that do not yet exist. This will commonly occur when a
variable has multiple channels that will not be created until the
custom constructor is run.  For these cases the variable will be
pre-emptively created and the preset value will be applied.  

This approach has the advantage of communicating network information
to the proc constructor from the network configuration - thereby
allowing the network programmer to influence the configuration of the
proc. instance.




Network Architecture and Theory of Operation
---------------------------------------------

A _caw_ graph is an ordered set of processors where a given
processor may contain a set of internal networks.
This leads to a heirarchy of networks and processors
that can be depicted like this.

[ diagram goes here.]

This diagram shows a two level network, where the internal
network contains an array of networks.

A processor instance is structured as a collection of variables along with a small set of
functions for maintaining it's state.
Variables act both as a means of holding the time varying state of the processor
and as input and output ports.  There is nothing preventing a variable from
being both an input and output port, although in practice then tend to be one
or the other.

As is the case in most dataflow implementations processors act as the nodes
of the graph and edges represent the flow of information between output and
input variables.  A _caw_ graph allows fan-out, multiple outputs from a variable,
but not fan-in.  A variable acting as in input may have only a single incoming edge.


Networks are executed sequentially, one processor at a time, from top
to bottom.  Networks that are members of the same network array,
referred to as silbing networks, may however execute concurrently to
one another.  To avoid concurrency hazards sibling networks may
therefore not contain any inter-connnections, and the language
precludes them.

The primary thread hazard with this arrangment arrangement is that
processors executing outside of the top level
should not write to global data or interact with
the global system API.

There is no way for the top level of a network to be a sibling
network, therefore processors that are part of this network are
guaranteed to run in sequence and be the only processor running while
they are executing.

For example, network presets can only be applied between execution
cycles (by the control application) or by a top-level processor.  This
is the case because it guarantees that no processors are running when
the preset values are set.


Note that the create() and destroy() calls for all processors
in the entire graph occur in a single thread and therefore do
not need to take multi-thread precautions - at least relative
to other _caw_ based execution.

Logging
-------

Even if the log is not yet instantiated calls to the log macros (e.g. cwLogError(), cwLogWarning() ...)
should at least print to the console.  This allows the log to be used by the application
for parsing the logs own user configuration parameters.

The log can be configured to use an internal non-blocking mulit-producer, single-consumer queue.
In this configuration all messages are initially stored in the queue and only
forwarded to the log output during a later call to `log::exec()`.  This avoids
the problem of the logging threads from blocking on the system locks which protects `stdout`.

```
log: { flags:[ `date_time`,      // Include date and time of each log call in the log output
               `file_out`,       // Write the log to the file named below in 'log_filename'.
			   `console`,        // Log to the system console.
			   `overwrite_file`, // If set overwrite the log file if it exists, otherwise the file name will be versioned to prevent over-writing.
			   `skip_queue`      // Do not use the internal queue. Send all log messages directly to the output.
			   ], 
       level:debug,              // Set the minimum log severity level: debug, info,warn,error,fatal
	   log_filename:"log.txt",   // Log filename (only used if 'overwrite_file' is set.
	   queue_blk_cnt:16,         // Count of blocks in the internal queue
	   queue_blk_byte_cnt:4096 } // Size of each block in the internal queuue
```

Proc Instance Logging
---------------------

Here is a complete proc instance log cfg.

```
log: { 
    
	   // level sets the overall verbosity from calls to proc_log_msg() 
	   // and the associated proc_error(),proc_debug(), ... macros
       level: debug,info,error,fatal
	   
	   // 'verbosity' controls message detail from variable logging
       verbosity:0=silent,  // turn off logging
                 1=minimal, 
				 2=summary, 
				 3=all, 
				 
       flags:[
		 all,   // log all proc. variables during initialize and runtime
		 init   // log all proc. variables at initialization 
	   ]
	   
	   // log specific proc. variables at initialization and runtime
       varL:[ 
		  'my_var'  // match label, 
		  'my_var0' // match label and sfxid 
		  ] 
		  
	 }
```

Specific variables are marked for logging based on the proc inst `log:{}` statement.
The logging then actually occurs in `proc_exec()` following execution of the proc custom exec function.
Variables whose changes can be tracked by var_set() are noticed and marked for output.
Variables that are buffers (abuf,fbuf,mbuf,rbuf) are output if they contain non-zero elements.
All other variable types are logged on every cycle.



Record Data Type
------------------

The primary reason to use a 'record' data type is to allow multiple
data values to be transmitted during a single cycle and be received as
a single incoming value.  In effect a record allows a structured table
of values to be transmitted during a single execution cycle.  For
example let's say that the output of a processor (G) is an irregular pulse
whose rate might be faster than the audio frequency.  This would
require multiple pairs of value (delta-time,amplitude) to be generated
during a given cycle.

Without the use of the record data type this would require the generating
processor to have two output variables 'dtime' and 'amplitude' which
would be updated multiple times during a single execution cycle.  The
receiving processor (R) would then need to respond to each of those
changes by implementing a value() function and storing the incoming
values in an internal array.  The stored values could then be acted
upon during the receiving processors exec() function.

If R didn't take this approach, and simply read
the incoming variables at the beginning of it's own execution cycle,
it would only see the value of the two output variables as they were left at the
end of the G execution cycle.  The previous values
transmitted during the execution cycle would be lost.

By explicitely transmitting a record G makes clear that multiple
values may be transmitted during a single execution cycle, while
providing the convenience R of automatically storing those value.
Furthermore the tranmission overhead is minimized by only transmitting
a single aggregate value rather than multiple individual values.

Another example, would be MIDI values that contain some additional
side information.  The MIDI data type already has the feature that it
can generate multiple messages per execution cycle. It's format
however is fixed. There is no way to add addtional information, like
score location, to each message. The fields of the record data type
however can hold any of the other data types.

Another reason to use the record data type is to simplify
the output and input interfaces to a processor and thereby
decrease the number of connections between processors.
They also make clear that a set of values is synchronized in time.
For example a set of (x,y) coordinates placed in a record
make it clear that the two values belong together in a way
that two input variables may not.

Finally, records are also very efficient. Given that the field index is
computed in advance (See `recd_type_field_index()`)setting and getting the field variable
is very fast.  As mentioned above transmitting a record avoids the overhead
of notifying receiving processors of every new value. The
receiving processor is only notified that the record
as a whole has changed.

Records are also implented in such a way that appending
a additition fields to an existing record is very fast.
The new record effectively inherits the contents of the
existing record by reference.  No data is copied.
For an example of this see the `vel_table` implementation.

Note that record field values may not be changed in place because
this would change the value for all other processors that
receive the record.  Incoming records fields must therefore be
copied if they are changed.

Note that it can be necessary to refer to a specific record data type
as used by another processor.  There two ways to do this:
1. If the record is explicitely defined in a proc class description
then it can be referred to by `"<proc_class>.<var_name>"` where
ever a record format is required.
2. The record may be defined and labelled in a network `records:{...}` statement
and then referred to by `"<label>"` where ever a record format is
required in a proc. instance statement.

See test/test.cfg `test_recd_registry` for examples of both of these uses.

Variable Change Notification
----------------------------
Processors are not *directly* notified when one of their connected
(a.k.a. input or destination) variables changes.  This is the case
for at least two reasons. First _caw_ does not implement a 'message' or 'event' propagation
scheme.  This limitation exists to prevent the need for a processor to
implement a thread-safe message handling scheme - since it is possible
that variable updates would come from concurrent threads executing the
processors which precede the destination proc. in the graph. 

Second there is no low cost, automatic, method of noticing that a
variable value changed.  The only practical way to do this would be to
rely on the programmer to remember to notify the system that a
variable value changed through some function call.  This seems error
prone since the processor is likely to work in every way but would
simply fail to notify downstream processors that the value changed.

If a processor really needs to notice when an input variable value
actualy changed it needs to cache the value and check if the value has
changed on the next execution cycle.  For a processor with many rarely
changing variables this can be both messy and wasteful since it is
possible that most of the variable will not change on any given
execution cycle.

To help handle this problem a variable change notfication scheme is built into the system
which notices when a variable *may* have changed. The system doesn't do 
any check, no matter the type of the variable, to see that it really did 
change value.  Instead it notices certain function calls and meta values that indicate
that the value may have changed. This scheme tends toward over reporting so
it is unlikely to miss a case where the value did change, but may report 
extra instances where the value didn't in fact change.

All variable notifications happen through a call to the destination proc's `notify()` function.
The call occurs just prior to, and in the same thread, as the destination proc's
`exec()` function.  See `proc_notif()`.

The class description of a destination variable must be marked with the `notify` flag
in order to receive notification calls.

To help handle this problem a variable change notfication scheme is built into the system.
When a variable description is marked with the 'notify' tag it is placed on
an internal list (`proc_t.modVarMapA[]`).  Just prior to the processors instance
exec() cycle a call to proc_notify() will result in a callback to the
instances notify() function for every marked variable that *may* have been modified since
the last execution of the instance.  Note that these callbacks happen
from a single thread, the same one that will subsequently call the exec() function.


Source variables  fall into one of three notification classes depending on their
value type. 

1. Auto-notify: These variable must use the `var_set()` mechanism to
change the value of the variable.  Variables in this category include
the numeric types and 'string'. `value_can_auto_notify()` returns true
for values that support this notification scheme.  Source variables in
this class use the `proc_t.modVarMapA[]` scheme to notify the
destination proc that the source valu may have changed.  Destination
variables fed by source variables in this category are placed in the
`modVarMapA[]` of their owning proc in `var_schedule_notification()` -
which is called from within the `var_set()` procedure on the source
variable. 

2. Buffers : These variables act as containers for their underlying
values `value_supports_an_element_count()` returns true for buffer
variable types. Variables in this category are: `abuf,fbuf,mbuf,rbuf`.
A change is noticed on these variables by looking at the count of
elements in their container.  If the count is non-zero (See
`value_has_elements_now()`) then the values are considered changed .

Destination variables, marked for notification, that are connected to 
this class of source variable are placed in the `manualNotifyVarA[]` 
of the destination proc during network creation. The destination proc scans this list
for just prior to each exec. cycle (See `proc_notify()`) and
signals notification on every variable whose source variable
has non-zero elements. Note that for `abuf` and `fbuf` variable this almost
always means  notificaiton triggers on every cycle.

3. Everthing else: These variables use the same mechanism as the 
'Buffer' variables but will trigger on every cycle.

Notification occurs at two different times: Prior to entering runtime
after entering runtime.  In many cases there may be little change in
state between just prior to entering runtime
(`proc->ctx->isInRuntimeFl==false`) and the first cycle
(`proc->ctx->isInRuntimeFl==false && proc->ctx->cycleIndex==0`).
Nonetheless notificaiton may occur for the same variables at both
times, even though the value of the source variable may not have
changed.

To avoid doing costly, one time setup, notice the value of `proc->ctx->isInRuntimeFl`
and perform this setup prior to entering runtime.


Threading:
--------

### Application Thread

The application thread is the thread provided by the main() function.
During the life of the program it runs in a loop and continually
calls the io::exec() function.
Exec:
   If a UI exists calls ui::ws::exec() to do websocket IO tasks.
     - This call may block, up to a programmable time-out duration, waiting for incoming websocket messages.
     - UI related callbacks to the application occur from this context.
     - If cfg. does not set the 'ui.asyncFl' then this thread may block on the IO callback mutex.

  If the audio meters are enabled then this these callbacks occur from this context.
     - If the audio group 'asyncFl' is not set then this call may block on the IO callback mutex.

  An 'exec' callback to the IO callback is made on every call to io::exec().
     - This call will block if the IO callback mutex is not available.


### Audio/DSP Thread
   Each audio device group uses a thread to coalesce device 'ready' states. When all devices in the group are ready
   (have empty playback and full record buffers) the thread unblocks.
   
### MIDI
    cwMidiDevice uses a thread to wait for incoming MIDI messages via poll(). (See `cwMidiDevice.cpp:_thread_func()`)
    - Incoming MIDI messages are fed to the IO callback via `cwIo::_midi_callback()`.
    

### Internal Server Threads
    
#### User threads
     - Thread machine threads which are created (See cwIo::threadCreate()) and run by the user - and only destroyed when the library is closed.

#### Thread-Once
    - Thread which is created, executes immediately by calling a user supplied callback function, and is later cleaned up by io::exec().
      This is useful for long running procedures where feedback (logs and/or progress) from the procuedure is useful to see as the procedure executes.
    
#### Timer
    - Thread machine hosted threads which block with a sleep call.
    
#### Sockets
    - Uses a thread machine based thread to wait for incoming socket events.  (See `cwIo.cpp:_socketCallback()`)
    
#### Serial
    - Based on cwSerialPortSrv which maintains it's own internal thread (See cwSerialPortSrv::threadCallback()) which blocks on the serial port file descriptors
    (See cwSerialPort.cpp:_poll())


Presets:
----------------

Preset description and application without the presence of 'poly' proc's is very straight forward:

For example:

```
example_1:
{

  network: {

    procs: {
      lfo:   { class: sine_tone, args:{ hz:3, dc:440, gain:110 }
        presets:
	{
	  ps_a:{ hz:2, dc:220, gain:55 },
	  ps_b:{ hz:4, dc:110, gain:220 },
	}
      }
      
      sh:    { class: sample_hold, in:{ in:lfo.out } }
      osc:   { class: sine_tone,   in:{ hz:sh.out },   args:{ ch_cnt:2 } },
      gain:  { class: audio_gain,  in:{ in:osc.out },  args:{ gain:0.3 } },
      aout:  { class: audio_out,   in:{ in:gain.out }, args:{ dev_label:"main"} }
    }
	
    presets:
    {
       a: { gain:{ gain:0.2 } },        //  One value sets both channels.
       b: { gain:{ gain:[0.1,0.3] } },  //  Multi-channel preset.
       c: { osc:a880 } },               //  Apply a class preset
       d: { osc:mono } },               //  Apply a class preset with an ignored 'init' variable.
       f: { osc:a220, lfo:ps_a } },     //  Apply a local preset and class preset
    }
  }
}
```

Calling `network_apply_preset(preset_label)` with one of the network preset labels 'a'-'f' will
work as expected in all of these cases.

Notes:

1. All preset values and proc/var's can be resolved at compile time.

2. Applying the network can be accomplished by resolving the network preset_label to a network_preset_t
in network_t.presetA and calling flow::var_set() on each attached preset_value_t.

3. Proc preset labels, as used in presets 'c','d','e' in the example, are resolved by first looking
for the label in the processor instance configation and then in the processor class description.


---

The following example shows how the preset processor labels can use suffix notation to address a range of processors.

```
example_2:
 {
  network: {
    procs: {
      osc:    { class: sine_tone, args: { ch_cnt:6, hz:[110,220,440,880,1760, 3520] }},
      split:  { class: audio_split, in:{ in:osc.out }, args: { select:[ 0,0, 1,1, 2,2 ] } },

      // Create three gain controls: g:0,g:1,g:2 using the processor label numeric suffix syntax.
      g0: { class:audio_gain, in:{ in:split0.out0 }, args:{ gain:0.9} },
      g1: { class:audio_gain, in:{ in:split0.out1 }, args:{ gain:0.5} },
      g2: { class:audio_gain, in:{ in:split0.out2 }, args:{ gain:0.2} },
                  
      merge: { class: audio_merge, in:{ in_:g_.out } },
      out:   { class: audio_out, in:{ in:merge.out },  args:{ dev_label:"main" }}
    }

    presets: {
      a: { g_:   { gain:0.1 } }, // Use suffix notation to apply a preset value to g0,g1,g2.
      b: { g0_2: { gain:0.2 } }, // Use suffix notation to apply a preset value to g0 and g1.
      c: { g2:   { gain:0.3 } }, // Apply a preset value to g2 only.
    }
  } 
}
```

---

```
example_3: {
   
  network: {

    procs: {

      // LFO gain parameters - one per poly voice
      g_list:  { class: list, args: { in:0, list:[ 110f,220f,440f ]}},

      // LFO DC offset parameters - one per poly voice
      dc_list: { class: list, args: { in:0, list:[ 220f,440f,880f ]}},
          
      osc_poly: {
        class: poly,
        args: { count:3 },  // Create 3 instances of 'network'.
           
        network: {
          procs: {
            lfo:  { class: sine_tone,   in:{ _.dc:_.dc_list.value_, _.gain:_.g_list.value_ }  args: { ch_cnt:1, hz:3 }},
            sh:   { class: sample_hold, in:{ in:lfo.out }},
            osc:  { class: sine_tone,   in:{ hz: sh.out }},
         },
	 
	 presets:
	 {
	   a:{ lfo:{ gain:0.1 } },
	   b:{ lfo:{ gain:0.2 } },
	   c:{ lfo0_1: { gain:0.3 }, lfo2:{ gain:0.4 } },
	  
	 }
       }               
     }

     // Iterate over the instances of `osc_poly.osc_.out` to create one `audio_merge`
     // input for every output from the polyphonic network.
     merge: { class: audio_merge,    in:{ in_:osc_poly.osc_.out}, args:{ gain:1, out_gain:0.5 }},
     aout:  { class: audio_out,      in:{ in:merge.out }          args:{ dev_label:"main"} }
    }

    presets: {
      a:{ osc_poly:a, merge:{ out_gain:0.3 } },
      b:{ osc_poly:b, merge:{ out_gain:0.2 } },
      c:{ osc_poly:c, merge:{ out_gain:0.1 } },
    }

  }
}
```

1. If a poly preset processor label does not have a numeric suffix then it is applied to all instances.
The alternative to this rule is to use an '_' suffix to imply 'all' processors of the given name.

2. A preset in an outer network may not directly address a processor in an inner network, however
it may select a named preset in an inner network.

3. Rule 2 can be generalized to: Network presets may only address processors which it contains directly - not nested processors.
In the example the outer most presets may therefore address the 'osc_poly' presets by label, but not
the processors contained by 'osc_poly'.

---
Presets with hetergenous poly networks
```
    example_03:
    {
      network: {

        procs: {

          osc_poly: {
            class: poly,
	    
	    // For het-poly networks the 'count' value given
	    // in the top level proc is the default value for
	    // the poly-count for following networks.
	    // This value may be overriden in the network
	    // definition itself - as it is in this example.
            args: { count:2, parallel_fl:true }, 
           
            network: {
	      net_a: {
	        count: 4,  // override higher level 'count'
		
                procs: {
                  osc:  { class: sine_tone,   args:{ hz: 100 }},
                },
		
		presets:
		{
		  a: { osc:{ hz:110 } },
		  b: { osc:{ hz:120 } },
		}
	      },
	      
	      net_b: {
	        count 3, // override higher level 'count'
                procs: {
                  osc:  { class: sine_tone,   args:{ hz: 200 }},
                },
		
		presets:
		{
		  a: { osc:{hz:220} },
		  b: { osc:{hz:230} }
		}
	      }
            },

            presets: {
	      aa: { net_a:a, net_b:a },
	      bb: { net_a:b, net_b:b },
	    }
           
         }

         // Iterate over the instances of `osc_poly.osc_.out` to create one `audio_merge`
         // input for every output from the polyphonic network.
         merge: { class: audio_merge,    in:{ in_:osc_poly.osc_.out}, args:{ gain:1, out_gain:0.5 }
	   presets: {
	     a:{ gain:0.3 }
	     b:{ gain:0.2 }
	   }
	 },
         aout:  { class: audio_out,      in:{ in:merge.out }          args:{ dev_label:"main"} }

	}
	
	presets: {
          a:{ osc_poly1:aa,   merge:a },
          b:{ osc_poly0_2:bb, merge:b },
          c:{ osc_poly:bb,    merge:{ out_gain:0.1 } },
	  d:{ osc_poly0:bb,   merge:{ out_gain:0.05} }
	}
      }
    }   
```
---
Dual Presets

---
Dual Presets with poly networks.

---
Dual Presets with heterogenous poly networks

---
Presets with user defined processors

---
Presets with user defined processors in poly networks.

---
Presets with user defined processors containing poly networks.


---
Use 'interface' objects to intercept preset values so that they
can be processed before being passed on to a the object that
they represent.

'interface' object have the same interface as the object to which their 'class' argument
refers but do nothing other than pass the values to their output ports.

```
example_4:
{

  network: {

    procs: {

      lfoIF: { class: interface, args:{ class:sine_tone } },

      // put a modifier here

      lfo:   { class: sine_tone, in:{ hz:lfoIF.hz, dc:lfoIF.dc, gain:lfoIF.gain } }
      sh:    { class: sample_hold, in:{ in:lfo.out } }
      osc:   { class: sine_tone,   in:{ hz:sh.out },   args:{ ch_cnt:2 } },
      gain:  { class: audio_gain,  in:{ in:osc.out },  args:{ gain:0.3 } },
      aout:  { class: audio_out,   in:{ in:gain.out }, args:{ dev_label:"main"} }
    }
	
    presets:
    {
       a: { lfoIF: { hz:1, dc:110, gain:55 } },
       b: { lfoIF: { hz:2, dc:220, gain:110 } },
    }
  }
}
```


Preset Implementation:
-----------------------
All presets are resolved to (proc,var,value) tuples when the networks are created.
A given named network preset is therefore a list these tuples.
Applying the preset is then just a matter of calling
var_set(proc,var,value) for each tuple in the list.
This pre-processing approach mostly avoids having to do value parsing
or variable resolution at runtime.


Preset dictionaries have the following grammar:
```
<preset-dict> -> <preset-label> : { <proc-label>: (<proc-preset-label> | <value-dict>) }

<value-dict> -> { <var_label>:(<literal> | [ <literal>* ])
```

A preset is a named ('<preset-label>') dictionary.
The pairs contained by the dictionary reference processors (<proc-label>).
The value of each pair is either a dictionary of variable
values (<value-dict>) or a label (<proc-preset-label>).

The <value-dict> is a collection of literal values
which can be directly converted to (proc,var,value) tuples.
In the case where a value is a list of literals the
individual values are used to address successive channels.
As part of variable resolution new variable channels will
be created if a preset references a channel that does
not yet exist on the given variable.  This guarantees
that the variable channel will be valid should the preset
be applied.

When the value of a preset pair is a <proc-preset-label>
the label may refer to one of three possible
source of preset variable values.
1. Processor class preset. This is a named <value-dict> defined with the processor class description.
2. Processor instance preset. This is a named <value-dict> defined with the processor instance.
3. Poly processor network preset. If the <proc-label> associated with
this <proc-preset-label> is a 'poly' processor then this label refers
to a network preset defined within the 'poly'  instances. 

Note that both <proc-label> and <var-label> strings may use 'suffix'
notation.  In the case of variables this allows the preset to target
specific 'mult' variable instances or ranges of instances.
When the network is a poly network using suffix notation
with a <proc-label> allows the target to particular
instances or ranges of instances.


Final Notes:

1. External network preset application requests that come from the control application
(e.g. caw::main()), or requests that occur from any processor that is not in the top level,
must be deferred until the end of the execution cycle when no processors are running.

Network preset application requests that occur from top level processors (processors running
in the outmost network can be applied directly because by definition the top level processors
run synchronously.

One way to handle this is to have a 'apply_preset' at the top level that takes
a preset label as input and applies it directly.

2. Maybe network presets should only be 'label' based and and processor instance
presets should only be 'value' based? Does this actually help anything?
Given that the system isn't currently limited in this way maybe it doesn't matter.

3. To Do:

- Processor instance presets have not been implemented.
- Preset application request deferrment has not been implemented.



  

Subnet (User Defined Proc) scheme:
-------------------------------------
```
{
    balanced_mix: {

	doc: "This is a two channel balancer network.",

    network: {
		ain:    { class: port, source:merge.in0, doc:"Audio input."},
		ain_alt:{ class: port, source.merge.in1, doc:"Alternate audio input."},
		bal_in  { class: port, type: real,       doc:"Mix balance control." },
				    
		bal:    { class: balance,     in:{ in:bal_in.out } },	    
		merge:  { class: audio_merge, in:{ in.0:ain, in.1:ain_alt } }
		gain:   { class: audio_gain   in:{ in:merge.out, gain:bal.out } },
	    
		aout:   { class: port, type: audio, in:{ gain.out } }
	  }
    } 
}
```
- Create a class description by parsing the subnet and converting 
the 'port' instances into a variable definitions.

- Port instances are just proc's that have a single variable but do not implement
any of the processing functions.  The variables act as 'pass-through' variables
that connect variables outside the subnet to variables inside the subnet.

- The subnet itself will be held inside an 'instance_t' and will pass
on 'exec()' calls to the internal instance processor chain.

- The current flow architecture only allows static connections.
This allows proc variables to be proxied to other proc variables.
This doesn't scale well for processes with many variables (e.g. matrix mixer).
For processes with many variables a message passing scheme works better
because it allows a message to dynamically address a process
(e.g. (set-in-channel-1-gain-to-42) 'set','in',1,'gain',42), 'set','out',4,'delay',15)

Note that it would be easy to form these messages on the stack and 
transmit them to connected processes. 


### Raspberry Pi Build Notes:

    cd sdk
    mkdir libwebsockets
    cmake -DCMAKE_INSTALL_PREFIX:PATH=/home/pi/sdk/libwebsockets/build/out -DLWS_WITH_SSL=OFF ..
    make
    sudo make install

    apt install libasound2-dev
    
