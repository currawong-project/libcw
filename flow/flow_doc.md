



## Proc Class Notation

```
<class-label> : { 
                  vars:    { (<var-label>:<var-desc>)* },
                  presets: { (<var-preset-label>:<var-preset>)* },
                  poly_limit_cnt: <int> 
               }

<var-desc> -> { type:<type-id>, value:<value-literal>, doc:<q-string>, flags:[ <attr-label> ] }

<var-preset> -> { <var-label> : <preset-value>  }

<preset-value> -> <value-list> | NUMBER | STRING | CFG

<value-list> -> [ (value-literal)* ] 
                    

```

### Applying class presets during proc instantiaion

The way class preset values get applied is a function of the type of the variable and the format of the value.
If a variable is not a _cfg_ type and the preset value is a list, or if the variable is a _cfg_ type
and the preset value is a list-of-containers then each each value in the list is assigned to successive
channels on the variable.  This is shown as the __multi-chan__ algorithm in the table below.
In all other cases the the value is applied to the 'any' channel, which is applied to 
all existing channels.

|    cfg      |   list  |   LoC           | chan | algorithm                                      |
| ----------- | ------- | --------------- | ---- | ---------------------------------------------- |
|      no     |   no    |     no          | any  | single-chan                                    |
|      no     |   no    |    yes          | n/a  | Value can't be an l-of-c w/o also being a list.| 
|      no     |  yes    |     no          |  N   | multi-chan                                     |
|      no     |  yes    |    yes          |  N   | multi-chan                                     |
|      yes    |   no    |     no          | any  | single-chan                                    |
|      yes    |   no    |    yes          | n/a  | Value can't be an l-of-c w/o also being a list.|
|      yes    |  yes    |     no          | any  | single-chan                                    |
|      yes    |  yes    |    yes          |  N   | multi-chan                                     |

When the class preset is applied during proc instantiation all variables have 
been instantiated with the base channel. This implies that the __single-chan__ 
algorithm is only setting the value of the the existig base channel.
The __multi-chan__ algorithm however will instantiate any channels above the base channel
that do not already exist by duplicating the base channel and then assigning the preset value.


### Notes

1. Legal `<type-id>` values

type     | Description
---------|-------------------------------------------------------------------------------------------------------------------
string   | 
bool     | `true` or `false`
int      | `int32_t`
uint     | `uint32_t`. The literal value must be suffixed with a 'u'. [See JSON Notes](#json-notes)
float    | Single precision float.  The literal value must be suffixed with an 'f'. [See JSON Notes](#json-notes)
double   | Double precision float
cfg      | JSON string
audio    |
spectrum | 
midi     |
srate    | (float) Sample rate type
sample   | (float) Audio sample type
coeff    | (float) Value that will be directly applied to a sample value (e.g added or multiplied)
ftime    | (double) Fractional seconds
runtime  | The type is left up to the processors custom 'create' function. These variables are not created automatically 
         | prior to calling the proc custom function.
all      | 
numeric  |


See list in cwFlowTypes.cpp : typeLabelFlagsA[]


2. Attribute labels indicate properties of this variable.

Attribute | Description
----------|--------------------------------------------------------------
`src`     | This variable must have a value or be connected to a source or
          | the proc cannot be instantiated, and therefore the network cannot be instantiated.
`src_opt` |
`no_src`  | This variable cannot be connected to a 'source' variable. Variables that are
          | only used as output usually have this property.
`init`    | This is an an initialization only variable. 
          | Changing the value during runtime will have no effect.
`mult`    | This variable may be instantiated multiple times by the in-statement.
          | Each variable is given a unique suffix id. Variables that do not have 
          | this property may only be instantiated once per proc-instance - albeit with possibly mulitple channels.
`out`     | This is a subnet output variable. [See Subnet Implementation](#subnet-implementation)
`no_ch`   | This variable will have only a single value and cannot be 'channelized'.

## Schema Notation

1. The schemas all describe JSON like structures. For clarity the dictionary braces and
list brackets are shown.

2. Variables are wrapped in `< >` markers.

3. Entitities wrapped in `( )*` indicate that the wrapped structure may be repeated 0 or more times.
Entitities wrapped in `( )+` may be repeated 1 or more times.

4. Variables with names ending in `label` indicate identifiers which are unique at least within
their local scope and possibly some greater scope.  For example proc-class names must be
unique across all loaded proc-class modules.

5. `<q-string>` indicates arbitrary text.

6. Entities are often optional.  Rather than clutter the notation with additional 
mark-up to indicate optional entitities this information is inclued in the notes 
that accompany each schema.





## JSON Notes

The 'cfg' language is a JSON like with some added features:

1. Comments can be inserted using C++ `\\` line comment and  `\* *\` block comment syntax.

2. Quotes are not necessary around strings that have no internal whitespace.

3. Numeric literals without decimal places are interpretted as integers, numbers
with decimal places are interpretted as doubles.  Unsigned and single precision float values
may be specified using suffix notation

Type     | Suffix   | Example
---------|----------|--------------
unsigned |   u      |  10u
float    |   f      |  12.34f 


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


## Proc Class Descriptions

```
```


## Proc Instance Syntax:

```
<label> : { 'class':<class>, "in":{<in_stmt>*}, "out":{<out_stmt>}, "preset":<class_preset_label> "args":<args_dict>, "log":<log_dict> }
```

Proc instance and variabel labels consist of two parts a leading identifier and a
numeric suffix.  The numeric suffix is referred to as the _label_sfx_id_.
A proc or variable label without a numeric suffix is automatically assigned the label suffix 0.


__args__ : This is a dictionary of named variable value records. 
__preset__ : This string references a class preset to use for initializing this proc instance.
__log__ : This is a dictionary of `<var_label>:<sfx_id>` pairs whose value should be printed to the console when they change at runtime.


Notes: 
1. All fields except 'class' are optional.
2. The class preset named by __preset__ is always applied before the __arg__ values referenced by  __argLabel__.




- When a preset is given as a list of values each entry in the list is taken as the value for
the associated channel.  When a list value is given to a var that is of type 'cfg' the list is passed
as a single value. 


## Processing Unit Class



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



# Applying Dual Presets

- When the network is instantiated the 'network-preset-pair' table is created.
This table has an entry for every proc/variable/channel instance in the network.

- When the preset preset-value lists are created the associated 'pair'
record is found in the 'network-preset-pair' table and stored in the preset-value
`pairTblIdx` field.

To apply a dual preset.
1. The preset-value list for the two presets are located.

2. The network-preset-pair value field is set to NULL.

3. The value associated with every preset-value in the secondary preset
is assigned to the associated 'value' field in the preset-pair-table.
The preset-pair record is found via a fast lookup using the `pairTblIdx` field.

4. For every preset-value in the primary preset-value list the matching
entry is found in the preset-pair-table. This is done via a fast lookup
using the `pairTblIdx` field.

5. If the preset-pair record value field is non-NULL then the primary preset-value's 
value field is then interpolated with the preset-pair record value field 
the associated network variable is set.

6. If the preset-pair record value field is NULL then the primary preset-value
is used to set the associated network variable.

# Subnet Implementation

Subnets are implemented in the following phases:
- During program initialization the subnet cfg files is scanned and the
proxy vars for each subnet are used to create a `class_desc_t` record for each subnet.

- When the subnet is instantiated the proxy vars are instantiated first.
- Then the internal network is instantiated.
- The proxy var's are then connected to the proxied vars. This may require
connecting in either direction: proxy->proxied or proxied->proxy, with the later
case being indicated by an 'out' attribute in proxied 'flags' list.
