Done
----
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


Names
------
ixon - 
hoot
caw, screech, warble, coo, peep, hoot, gobble, quack, honk, whistle, tweet, cheep, chirrup, trill, squawk, seet, 
cluck,cackle,clack
cock-a-dooodle-doo
song,tune,aria
