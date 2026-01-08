Done
====

12/2025 to 01/2026
------------------
- DONE: Implement 'Reload Program' in caw.
    + Allow the `program` part of the main 'cfg' file to be reloaded from a button on the panel.
	
### Logging
- DONE: Convert the logging object to use the multiple-producer/single-consumer queue (cwNbMpScQueue).
  Add an exec call which will be called by the main app to empty the queue to the console, the UI
  or both.

- DONE: Logs should be writable to a file, or sent to stdout via the UI selection controls on each proc.

- DONE: Add an io.cfg parameter which will turn on/off automatic logging to the console.

- DONE: Consider printing to the console from the CAW app during idle time or asynchronously.
  This might not be an issue at all if the CAW UI is on a separate machine.
  

- There should be special logging macros inside procs that automatically log the instance name.
  Likewise there should be special cwLogErrorProc(rc,proc), cwLogErrorVar(rc,var) to automatically report the source of the error.
  DONE: See `proc_error()` and `proc_warng()`

- Add 'level' to the proc instance `log` statement.  This value can then be used to filter `proc_error(),proc_info() ...`
  DONE: See `proc_log_msg()` and `cwFlowNet.cpp _proc_log_stmt_parse_level()`
		
- DONE: proc instance `log` feature: 
  + should print the values for all channels - right now it is only printing the values for kAnyChIdx	  
  + log should print values for abuf (mean,max), fbuf (mean,max) mag, mbuf
  + Add 'time' type and 'cfg' types to a 'log:{...}' stmt.
  + doesn't work for types that do not call `var_set()`.
  + TODO: 
    Strategy 1: Simply log all variables that are marked for logging as a post exec() process.
    If a variable type uses `var_set()` then add it to the log var list when it is set.
	Complex types don't use `var_set()` and so they will be printed on on every pass.  
	Note that buffer var's (abuf,mbuf,rbuf,fbuf) will not be printed if they are empty).
	Matrix types should print in abreviated form by default, or in full if requested.
	'cfg' vars will print in abbreviated form,or in full if requested.
	
	Strategy 2: The 'output' value of all complex types (abuf,cfg,mtx) is set to null 
	as a pre-process to `exec()`.  Note that `var_get()` will still return their last value
	but `var_set()` must be called to publish a new value.
	
	
	Complex variables that are connected to sources or tagged as 'init' are exempt from this process.
    Init variables should only be logged on cycle 0.
	
	DONE: Specific variables are marked for logging based on the proc inst `log:{}` statement.
	The logging then actually occurs in `proc_exec()` following execution of the proc custom exec function.
	Variables whose changes can be tracked by var_set() are noticed and marked for output.
	Variables that are buffers (abuf,fbuf,mbuf,rbuf) are output if they contain non-zero elements.
	All other variable types are logged on every cycle.

### UI

- The UI should not be updated on every DSP cycle.
   + Add sub-cycle index/count to audio update or possibly better give
     a UI update time in milliseconds and only generate the update
     when the audio update passes that duration.
  DONE: See the `ui_update_ms` network parameter.

- Rewrite `_create_var_list()` in cawUI.cpp. The current scheme uses separate columns for the labels
  and UI widgets. Change the layout so that the label and the widgets share a div - where the first
  widget includes the label and the following labels (on successive channels) do not have labels.
  + test by displaying proc's with multiple channels. 
  DONE. 
  
- Disable widgets that are connected or have the 'init' attribute. If all the channels of a variable
  are disabled then set the variable label to grey. 
  DONE: See cwFlowNet.cpp `_fill_net_ui_proc_and_preset_arrays()`
    
- Add a UI label to the flow var description.  As it is the variable label is always the UI label
  but the variable label has certain lexical requirements which make it a bad UI label. 
  DONE Set `title` via proc inst `ui:{}` cfg.
  

- Improve the automatic layout of proc inst UI's.  In particular improve the packing density.
  Add a way to manually layout the proc instances. 
  DONE: 12/21/25: proc's now fill consecutive columns.

- Disabled `disp_str` should turn grey.
  DONE: Added `disable_able` to `disp_str` UI element.
  
- Decide how best to handle enabling and disabling proc UI's.
  The current method is not quite right. Proc instance should always override network setting.
  DONE See proc inst `ui:{}`

### Crash Bugs:
- CRASH BUG: Remove all 'resource' files from `ex_11_score_follower_rt` and attempt to run program. CRASH! (should produce error messages)
  DONE: piano_score::create() was not returning a failure code correctly


- CRASH BUG: if the destination of an 'out:{}' connection (e.g. a connection to an earlier proc. in the exec. chain)
  is already connected the network parser crashes. This is easy to reprodue by
  having two procs with out:{} statements route to the same destionation variable.
  DONE: var_disconnect() was not nulling the `dst_link` in the src. var.

- CRASH BUG: Check for duplicate field labels. Particularly when using a base record.
  It's easy to declare a field named 'midi' and then inherit a record
  with a field named 'midi'.
  DONE: See `recd_type_create()` and `recd_array_create()`.

- CRASH BUG: flow:`recd_merge` and `recd_copy()` is extremely dangerous.  It is easy to end up merging incompatible record types into the
  same output.  If bitwise copying is used the record types should be verified to be identical. See `recd_extract`
  for an example of working with `recd_type` to do a safe field by field copy.
  DONE: `recd_merge` has been rewritten to avoid copying and `recd_copy()` has been removed.

### Record data type

- Notice that when proc's are instantiated output record variables use
  a copy-and-pasted function called `_alloc_recd_array()` and then call
  `var_regster_and_set()` on the output variable. These two operations could be combined.
  DONE: All copies of `_alloc_recd_array()` were removed and replaced with calls to `var_alloc_register_and_set()` 

### Variable Notification

- All outputs must be set via `var_set()` call, otherwise proc's that
  rely on noticing changed variables will not work.  For example the
  logging and the 'print' proc does not work for record,midi,audio
  because in general these types are not set via calls to `var_set()`.
  Implementing this might mean that `abuf_t,mbuf_t,rbuf_t` would need
  to be instantiated automatically, thereby forcing the proc
  programmmer to use `var_set()` and `var_get()` to access their
  values.  This might also be a good thing because it would allow the
  buffers to be automatically emptied (e.g. setting `rbuf_t->recdN` to
  0) by the system thereby relieving the proc programmer from having
  to remember to do it.
  
  Notes: 
  0. Having all variables call `var_set()` every time they change
  is too error prone and isn't necessay to the basic working of the
  system. 
  
  1. logging should be a handled as a post exec process where
  itegral variables are placed on a list inside var_set() but
  non integral types print always (e.g. cfg,tx) or if then are 
  not empty (e.g.abuf,fbuf,mbuf,rbuf).
  
  2. A notification request on non-integral types should generate
  and error or warning - since it doesn't make any sense.
  (i.e. there isn't really a good way to notice that a non-integral
  type has changed  - they should simply be read by the destination
  proc every time - and assume that they have changed.) If in doubt consider recognizing a change
  in a 'cfg' type variable.  If it is really necessary to know that that
  a non-integral value has changed then add an indpendent 'changed' variable
  to the proc.  In general this functionality isn't so important that
  it needs to be built in.
  DONE: See cwFlowNet.cpp `_proc_create_var_map()` and `Variable Change Notification` in notes.md


- Variable change notification can work for midi,audio, and record by simply noticing that the size of the
  buffer is not zero.  ... it's 'cfg' and 'mtx' types where there is a bigger problem noticing changes.
  DONE: This behaviour is not implemented via the `proc_t.manualNotifyVarA[]`


### Misc

- check for memory allocation and release during runtime and issue a warning
  DONE: See `mem::set_warn_on_alloc()` 

- Check for illegal variable names in class descriptions.  (no periods, trailing digits, or trailing underscores)
  DONE: See `cwFlowTypes.cpp var_desc_create()`

- Add a reduced API that eliminates the need to use  kAnyChIdx in `var_set()/var_get()` calls.
  DONE: See `var_get()` and `var_set()` that do not require kAnyChIdx.

- Verify that all variables have been registered (have valid 'vid's) during post instantiation validation.
  (this is apparently not currently happening)
  DONE: See `proc_vaidate()`

- Check for duplicate network names in a program cfg.
  DONE: See `cwIoFlowCtl.cpp create()`


Pre-12/2025
----------
- DONE: Why doesn't the C7 on the downbeat of meas. 11 sound? (... it does but is quiet due to velocity table)
- DONE: Allow setting the location of the score player.  This should also reset the sampler and voice control.
- DONE: The voice ctl should respond to all-notes-off message and reset each sampler channel.

- DONE: Remove `preset_label` and `type_src_label` from `_var_channelize()` and report error
locations from the point of call.

- DONE: Move proc_dict.cfg to libcw directory.

- DONE: The proc inst 'args' should be able to create mult variables. The only way to instantiate
new mult variables now is via the 'in' stmt.

- DONE: The `audio_merge` implementaiton is wrong. It should mimic `audio_mix` where all igain
coeff's are instantiated even if they are not referenced.

- DONE: Add the `caw` examples to the test suite.


- DONE: Remove the multiple 'args' thing and and 'argsLabel'.  'args' should be a simple set of arg's.  

- DONE: Compile presets: at load time the presets should be resolved
  to the proc and vars to which they will be assigned.


- DONE: (We are not removing the kAnyChIdx) 
Should the var's with multiple channels remove the 'kAnyChIdx'?
This may be a good idea because 'kAnyChIdx' will in general not be used
if a var has been channelized - and yet it is possible for another 
var to connect to it as a source ... which doesn't provoke an error
but would almost certainly not do what the user expects.
Note that the kAnyChIdx provides an easy way to set all of the channels
of a variable to the same value.

- DONE: verifiy that all proc variables values have a valid type - (i.e. (type & typeMask) != 0)
  when the proc instance create is complete. This checks that both the type is assigned and
  a valid value has been assigned - since the type is assigned the first time a value is set.
  
- DONE: 'poly' should be implemented as a proc-inst with an internal network - but the 
elements of the network should be visible outside of it.

- DONE: 'sub' should be implemented as proc-inst with an internal network, but the
elements of the network should not be visible outside of it. Instead it should
include the idea of input and output ports which act as proxies to the physical
ports of the internal elements.

- DONE: 'poly' and 'sub' should be arbitrarily nestable. 

- DONE: Allow multiple types on an input.
   For example 'adder' should have a single input 
   which can by any numeric type.
   

- DONE: Make a standard way to turn on output printing from any port on any instance
This might be a better approach to logging than having a 'printer' object.
Add proc instance field: `log:{ var_label_0:0, var_label_1:0 } `

- Complete user-def-procs:
	+ User-Def-Procs should have presets written in terms of the user-def-proc vars rather than the network vars
	or the value application needs to follow the internal variable src_var back to the proxy var.

	+ DONE: write a paragraph in the flow_doc.md about overall approach taken to user-def-proc implementation.

	+ DONE: user-def-proc var desc's should be the same as non+user-def-proc vars but also include the 'proxy' field.
	In particular they should get default values.
	If a var desc is part of a user-def-proc then it must have a proxy.
	The output variables of var desc's must have the 'out' attribute


	+ DONE: improve the user-def-proc creating code by using consistent naming + use proxy or wrap but not both

	+ DONE: improve code comments on user-def-proc creation

  
- DONE: Implement feedback	

- DONE: Implement the ability to set backward connections - from late to early proc's.
  This can be done by implementing the same process as 'in_stmt' but in a separate 
  'out_stmt'. The difficulty is that it prevents doing some checks until the network
  is completely specified.  For example if audio inputs can accept connections from 
  later proc's then they will not have all of their inputs when they are instantiated.
  One way around this is to instantiate them with an initial set of inputs but then
  allow those inputs to be replaced by a later connection.

BUGS:
- DONE: The counter modulo mode is not working as expected.



- DONE: Implement 'preset' proc. This will involve implementing the 'cfg' datatype.

- DONE: Finish the 'poly' frawework. We are making 'mult' var's, but do any of the procs explicitly deal with them?

- DONE: Turn on variable 'broadcast'.  Why was it turned off? ... maybe multiple updates?

- DONE: There is no way for a proc in a poly context to use it's poly channel number to 
select a mult variable.  For example in an osc in a poly has no way to select
the frequency of the osc by conneting to a non-poly proc - like a list.
Consider: 
1. Use a difference 'in' statememt (e.g. 'poly-in' but the 
   same syntax used for connecting 'mult' variables.)
2. Include the proc name in the 'in' var to indicate a poly index is being iterated
   e.g. `lfo: { class:sine_tone, in:{ osc_.dc:list.value_ } }` 

- DONE: Fix up the coding language - change the use of `instance_t` to `proc_t` and `inst` to `proc`, change use of `ctx` in cwFlowProc



DONE: After the network is fully instantiated the network and class presets
are compiled.  At this point all preset values must be resolvable to
an actual proc variable.  A warning is issued for presets with values
that cannot be resolved and they are disabled.  The primary reason
that a preset might not be resolvable is by targetting a variable
channel that does not exist.

- DONE: All cfg to value conversion should go through `cfg_to_value()`.

- DONE Presets do not work for hetergenous networks.

- DONE: Consider eliminating the value() custom `proc_t` function and replace it by setting a 'delta flag' on the
variables that change.   Optionally a linked list of changed variables could be implemented to
avoid having to search for changed variable values - although this list might have to be implemented as a thread safe linked list.

- DONE: value() should return a special return-code value to indicate that the
value should not be updated and distinguish it from an error code - which should stop the system.
Note: This idea is meaningless since variables that are set via connection have no way to 'refuse'
a connected value - since they are implemented as pointers back to the source variabl.e


- DONE: Allow proc's to send messages to the UI. Implementation: During exec() the proc builds a global list of variables whose values
should be passed to the UI. Adding to the list must be done atomically, but removing can be non-atomic because it will happen
at the end of the network 'exec' cycle when no proc's are being executed.
See cwMpScNbQueue push() for an example of how to do this.

- DONE: Add a 'doc' string-list to the class desc.

- DONE: It is an error to specify a `suffix_id` on a poly network proc because the `suffix_id`'s are generated automatically.
  This error should be caught by the compiler.

- DONE: Add a 'preset' arg to 'poly' so that a preset can be selected via the owning network.
  Currently it is not possible to select a preset for a poly.

- DONE: Automatic assignment of `sfx_id`'s should only occur when the network is a 'poly'.
  This should be easy to detect.
  
- DONE: If a proc, inside a poly,  is given a numeric suffix then that suffix will
  overwrite the `label_sfx_id` assigned by the system.  This case should be detected.
  
- DONE: Re-write the currawong circuit with caw.

- DONE: Check for unknown fields where the syntax clearly specifies only certain options via the 'readv()' method.



Names
------
ixon - 
hoot
caw, screech, warble, coo, peep, hoot, gobble, quack, honk, whistle, tweet, cheep, chirrup, trill, squawk, seet, 
cluck,cackle,clack
cock-a-dooodle-doo
song,tune,aria
