


# Flow Documentation:

_flow_ is an experimental audio processing framework which supports 
the execution of real-time and non-real-time music 
and audio processing networks and the application of network state data. 

The framework is designed to easily build a certain flavor of 
audio processing network which is defined by the ability
to arbitrarily and seemlessly update the state of the network in real-time.


A _flow_ program is a data flow network formed from a collection of
interconnected processing units.  A processing unit is referered
to as a __proc__. Each processing unit has a function which 
operates on a set of variables. The network is formed
by connecting processors together via their variables.


Fig. 1 shows a simple network non-real time network
where the output of a sine oscillator is written to an audio file.








## Building Neworks:


### A Simple Network.
```
```

Use `$` prefix to a filename. Use proc_expand_filename() to prepend `proj_dir` to a file prefixed with `$`.

### Polyphonic Network.

### Network with sub-nets.

## Proc Instance Syntax:

```
<label> : { 'class':<class>, "in":{<in_stmt>*}, "preset":<class_preset_label>, "log":<log_dict> "argLabel":<arg_preset_label>, "args":<args_dict> }
```

Proc instance and variabel labels consist of two parts a leading identifier and a
numeric suffix.  The numeric suffix is referred to as the _label_sfx_id_.
A proc or variable label without a numeric suffix is automatically assigned the label suffix 0.


__args__ : This is a dictionary of named variable value records. 
__preset__ : This string references a class preset to use for initializing this proc instance.
__argLabel__ : This string references an `args` dictionary parameter set to be applied after the __preset__ class preset.
If this argument is not given then it is automatically assigned the value "default". (What if there is not __arg__ record named default?
What if the are no __arg__ records at all?)
__log__ : This is a dictionary of `<var_label>:<sfx_id>` pairs whose value should be printed to the console when they change at runtime.


Notes: 
1. All fields except 'class' are optional.
2. The class preset named by __preset__ is always applied before the __arg__ values referenced by  __argLabel__.




- When a preset is given as a list of values each entry in the list is taken as the value for
the associated channel.  When a list value is given to a var that is of type 'cfg' the list is passed
as a single value. 


## Processing Unit Class

type   | Description
-------|-------------------------------------
string | 
bool   |
int    | `int32_t`
uint   | `uint32_t`
float  | f32
double | f64
srate  | (float) Sample rate type
sample | (float) Audio sample type
coeff  | (float) Value that will be directly applied to a sample value (e.g added or multiplied)
ftime  | (double) Fractional seconds
runtime| The type is left up to the processors custom 'create' function. These vars are not automatically created.

See list in cwFlowTypes.cpp : typeLabelFlagsA[]


### Processing Units (proc)

A 'process' or **proc** is a set of functions and **variables**.

A **network** is a set of proc's with interconnected variable.
By default most proc variables can be connected to, or act as sources for,
other proc variables.  A variable consists of a label, a type (e.g. int,real,audio,...),
some attribtes (more about those below), a default value, and a documentation string. 

One of the goals of 'flow' is to naturally handle multi-channel audio signals.
To this end many audio processors create multiple internal sub-processes
to handle each audio channel based on the number of input audio channels.

For example an audio gain processor consists of three variables: an input audio variable,
an output audio variable and a gain coefficient. When the gain unit is
created it will create an independent sub-process to handle each channel.
If the input audio signal has multiple channels than the gain processor
will internally duplicate each of the three variables. This allows
independent control of the gain of each of the audio channels.
In 'flow' parlance each of the sub-processes is referred to as a **channel**.
A variable that can duplicated in this way is referred to as a **multi-channel variable**.

'flow' has a second form of variable multiplicity. This form occurs when
a variable is duplicated based on how it is connected in the network.
For example a simple audio mixer might have an undefined number of audio inputs
and a single audio output. The number of audio inputs is only defined
once it is connected in the network.  This notion of variable multiplicity
is different from a **channel** because each of the incoming audio
signals may themselves contain multiple channels - each of which should
be individually addressable.  However, it should also be possible
to address each of the incoming signals as a single entity. To
accomplish this we use the concept of the **mult-variable**. A mult-variable
amounts to creating an array of variables based on a single variable description,
where the array length is determined at network compile time. mult-variables
are distinguished by labels with integer suffixes.








The functions are:

Name     | Description
---------|------------------------------------------------------------------------
create   | Implements the custom elements of the proc instantiation.
destroy  | Destroy resources that were acquired in create().
value    | Variable values will pass through this function as they are assigned.
exec     | Implements the custom execution functionality of this process.
report   | Print the state of the process.







### Var Syntax

__label__ : { type: __type__, { value: __value__ }, {proxy: __proxy__}, {flags:[{__flag__}*]}, doc:"q-string" }

Part   | Description
-------|-------------------------------------------------------
label  | Variable name
type   | Variable type.  See Data types below.
value  | The default value of the variable.
proxy  | 
doc    | Documentation string for this variable.
flags  | 

Notes:

- Whenever possible default values should be provided for the
variable - even if the value is meaningless - like 0.0.  This is
important because during proc instantiation, prior to the custom
create() call, variables listed in the proc instance's 'in' statement
are connected to their respective sources.  If the source does not
have a valid value then the instantiation will fail.  This consitutes
a failure because it is guaranteed that when the custom create()
function is called all variables in the 'in' statement will be
resolved to a source variable with a valid value.  This allows the
proc instance to have the information it needs to configure itself.

There is one subtle case where the default value becomes important. When the
variables in an 'in' statement are initially connected to their source they are
connected to the 'any-channel' source variable because they
do not have a specific channel yet.  Specific channel can only be known
during or after the custom create() function is called.  Since the way
a given proc. distributes channels will vary from one proc. to the
next.

If during initial variable connection the source happens to be a
variable with channels then the values that were assigned to those
channels, in the source proc. create() function, will not show up on
the 'any-channel'.  If no default value was assigned to the source
variable then the 'any-channel' will have no value, and the connection
will fail with an error message like this: 

```
"The source value is null on the connection input:foo:0 source:blah:0.bar:0".
```

Note that although variables are initially connected to the
'any-channel' source variable, prior to the their proc's create() call,
after the create() call, when the variables do have a valid channel
number, they are reconnected to a specific channel on the source
variable.


### Var Semantics

#### Var Types:

- Variables final types are determined during their owner proc instantiation.
Once the type is set it never changes for the life of the proc.

- When reading a variable value the value will be coerced to the type of the output variable.
For example:  `int v; var_get(var,v)` will coerce the value of `var` to an `int`.

- When writing a variable the value will be coerced to the type of the variable.
For example: If the type of `var` in `var_set(var,float_val)` is `double` then the value of `float_val` will be coerced to a double.

- The type a variables value is set in `variable_t.type` and always consists of a single bit field.
(i.e. `assert(isPowerOfTwo(variable_t.type))`)

- The type of the value assigned to a variable (`variable_t.value->tflag`) must always exactly match `variable_t.type`.

### Preset Syntax:


### Data Types:

Types    | Description             |
---------|-------------------------|
bool     | `bool`
int      | `int32_t`
uint     | `uint32_t`
real     | `double`
audio    | multi-channel audio
spectrum | multi-channel spectrum
cfg      | 
srate    | platform defined sample rate type
sample   | platform defined audio sample type
coeff    | platform defined signal processing coefficient type


### Variable Flags:

Flag             | Description
-----------------|-------------------------------------------------------------------------------------------
`init`           | This variale is set at proc instantation and never changes.
`src`            | This variable must be connect to a source variable in the instance 'in' statement or be set to a default value. See 1.
`no_src`         | This variable may not be connected to a source variable. See 1.
`no_dflt_create` | This variable is not created automatically as part of the proc instantiation. See 2.
`mult`           | This variable may be duplicated in the instance 'in' statement. See 3.


Notes:
1. Unless the `no_src` attribute is set any variable may be connected to a source variable
in the proc instantation 'in' statement.  `no_src` variables are output variables whose
value is calculated by the proc and therefore don't make sense to be fed from 
some other entity.


2. By default all variables are created prior to the proc `create()` function being called.
Variable with the `no_dflt_create` attribute will not be created.  This is useful in cases
where information needs to be accessed in the `create()` function in order to determine
some basic parameters of the variable  For example a proc that needs to create multiple output
variables based on the number of input audio channels cannot know how many output variables
it may need until it accesses the number of audio channels it has been instantiated with.


