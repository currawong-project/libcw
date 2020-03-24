var _ws = null;
var _dfltParentId = "uiDivId";

function set_app_title( suffix, className )
{
  var ele = document.getElementById('appTitleId');
  ele.innerHTML = "UI Test:" + suffix
  ele.className = className
}


function uiOnError( msg, r)
{
  console.log("Error:" + msg);
}

function uiGetParent( r )
{
  parent_ele = document.getElementById(r.parent_id);
  
  if( parent_ele == null )
  {
    uiOnError("Parent not found. parent_id:" + r.parent_id,r);
  }

  return parent_ele;
}

function uiCreateEle( r )
{
  var parent_ele;
  
  if((parent_ele = uiGetParent(r)) != null )
  {    
    ele            = document.createElement(r.ele_type)
    ele.id         = r.ele_id;
    ele.className  = r.value;
    
    parent_ele.appendChild(ele)
  }
}

function uiRemoveChildren( r )
{
  ele = document.getElementById(r.ele_id)
  
  while (ele.firstChild)
  {
    ele.removeChild(ele.firstChild);
  }  
}

function uiDivCreate( r )
{ uiCreateEle(r) }

function uiLabelCreate( r )
{
  var parent_ele;
  
  if((parent_ele = uiGetParent(r)) != null )
  {
    ele            = document.createElement("label")
    ele.htmlFor    = r.ele_id
    ele.innerHTML  = r.value;
    parent_ele.appendChild(ele)
  }
  
}

function uiSelectCreate( r )
{
  uiCreateEle(r)
}
  
function uiSelectClear( r )
{ uiRemoveChildren(r) }

function uiSelectInsert( r )
{
  var select_ele;
  
  if((select_ele = uiGetParent(r)) != null )    
  {
    var option    = document.createElement('option');

    option.id        = r.ele_id;
    option.innerHTML = r.value;
    option.value     = r.ele_id;
    option.onclick   = function() { uiOnSelectClick(this) }
  
    select_ele.appendChild(option)
  }
}

function uiSelectChoose( r )
{
  var select_ele;
  
  if((select_ele = uiGetParent(r)) != null )    
  {
    if( select_ele.hasChildNodes())
    {
      var children = select_ele.childNodes
      for(var i=0; i<children.length; i++)
      {
	if( children[i].id == r.ele_id )
	{
	  select_ele.selectedIndex = i
	  break;
	}	
      }
    }
  }
}

function uiOnSelectClick( ele )
{
  cmdstr = "mode ui ele_type select op choose parent_id "+ele.parentElement.id+" option_id " + ele.id
  websocket.send(cmdstr);

}

function uiNumberOnKeyUp( e )
{
  if( e.keyCode == 13 )
  {
    //console.log(e)
    cmdstr = "mode ui ele_type number op change parent_id "+e.srcElement.parentElement.id+" ele_id " + e.srcElement.id + " value " + e.srcElement.value    
    websocket.send(cmdstr);
  }
}


function uiNumberCreate( r )
{
  var parent_ele;
  
  if((parent_ele = uiGetParent(r)) != null )        
  {
    ele    = document.createElement("input")
    ele.id = r.ele_id
    ele.setAttribute('type','number')
    ele.addEventListener('keyup',uiNumberOnKeyUp)
    parent_ele.appendChild(ele)
  }
}

function uiNumberSet( r )
{
  var ele;

  console.log("ele_id:" + r.ele_id + " parent_id:" + r.parent_id + " value:" + r.value)
  
  if((ele = document.getElementById(r.parent_id)) != null)
  {
    switch( r.ele_id )
    {
      case "0":  ele.min   = r.value; break;
      case "1":  ele.max   = r.value; break;
      case "2":  ele.step  = r.value; break;
      case "3":  ele.value = r.value; break;
    }
  }
}


function dom_id_to_ele( id )
{ return document.getElementById(id); }

function dom_child_by_id( parentEle, child_id )
{
  var childrenL = parentEle.children
  for(var i=0; i<childrenL.length; i++)
  {
    if( childrenL[i].id == child_id )
    {
      return childrenL[i];
    }
  }
  return null;
}

function dom_set_option_by_text( ele_id, text )
{
  var ele = dom_id_to_ele(ele_id);
  
  for (var i = 0; i < ele.options.length; i++)
  {    
    if (ele.options[i].text === text)
    {
        ele.selectedIndex = i;
        break;
    }
  }
}

function dom_set_checkbox( ele_id, fl )
{ dom_id_to_ele(ele_id).checked = fl }

function dom_get_checkbox( ele_id )
{ return dom_id_to_ele(ele_id).checked }


function dom_set_number( ele_id, val )
{
  dom_id_to_ele(ele_id).value = val
}

function ui_error( msg )
{
  console.log("Error: " + msg )
}

function ui_print_children( eleId )
{
  var childrenL = document.getElementById(eleId).children
    
  for(var i=0; i<childrenL.length; i++)
  {      
    console.log( childrenL[i] )
  }      
}

function ui_get_parent( parentId )
{
  if( parentId==null || parentId.trim().length == 0 )
    parentId = _dfltParentId
  
  parent_ele = document.getElementById(parentId);
  
  if( parent_ele == null )
  {
    ui_error("Parent element id: " + parentId + " not found.")
  }

  return parent_ele;  
}


function ui_create_ele( parent_ele, ele_type, d )
{
  // create the ctl object
  var ele = document.createElement(ele_type);

  if( ele == null )
    ui_error("'%s' element create failed.", ele_type);
  else
  {
    ele.id    = d.jsId;
    ele.uuId  = d.uuId;
    ele.appId = d.appId;
    parent_ele.appendChild(ele);
  }
  return ele
}

function ui_create_ctl( parent_ele, ele_type, label, d )
{
  // create an enclosing div
  var div_ele = document.createElement("div");

  div_ele.className = d.clas;

  parent_ele.appendChild( div_ele );
  
  var label_ele = div_ele
  
  // if label is not null then create an enclosing 'label' element
  if( label != null )
  {
    label_ele = document.createElement("label");

    label_ele.innerHTML = label;

    div_ele.appendChild(label_ele)    
  }

  return ui_create_ele( label_ele, ele_type, d );
}


function ui_create_div( parent_ele, d )
{
  var div_ele =  ui_create_ele( parent_ele, "div", d );


  if( div_ele != null )
  {
    div_ele.className = d.clas
    
    var p_ele = document.createElement("p")
    
    if( d.title != null && d.title.length > 0 )
      p_ele.innerHTML = d.title
    
    div_ele.appendChild( p_ele )
  }
  
  return div_ele
}

function ui_create_title( parent_ele, d )
{
  return ui_create_ele( parent_ele, "label", d );
}

function ui_create_button( parent_ele, d )
{
  var ele = ui_create_ctl( parent_ele, "button", null, d );

  if( ele != null )
  {
    ele.innerHTML = d.title;
    ele.onclick   = function() { _ws.send("value " + this.uuId + " i : 1"); }
  }
  
  return ele
}

function ui_create_check( parent_ele, d )
{
  var ele = ui_create_ctl( parent_ele, "input", d.title, d )
  
  if( ele != null )
  {
    ele.type = "checkbox";

    dom_set_checkbox(ele.id, d.value );    
    
    ele.onclick = function() {  _ws.send("value" + this.uuId + " b : " + dom_get_checkbox(this.id)); }
  }
}

function ui_on_select( ele )
{
  var s = "value " + ele.uuId + " i : " + ele.options[ ele.selectedIndex ].appId

   _ws.send( s );
}

function ui_create_select( parent_ele, d )
{
  var sel_ele = ui_create_ctl( parent_ele, "select", d.title, d );
  sel_ele.onchange = function() { ui_on_select(this) }
  return sel_ele;
}

function ui_create_option( parent_ele, d )
{
  var opt_ele = ui_create_ele( parent_ele, "option", d );

  if( opt_ele != null )
  {
    opt_ele.className = d.clas;
    opt_ele.innerHTML = d.title;
  }
  
  return opt_ele;
}

function ui_create_string( parent_ele, d )
{
  var ele = ui_create_ctl( parent_ele, "input", d.title, d );

  if( ele != null )
  {
    ele.value = d.value;
    ele.addEventListener('keyup', function(e) { if(e.keyCode===13){  _ws.send("value" + this.uuId + " s : " + this.value + "\0");} } );
  }
}

function ui_number_keyup( e )
{
  console.log(e)
  if( e.keyCode===13 )
  {
    var ele = dom_id_to_ele(e.target.id)
    console.log(ele.value)
    if( ele != null )
      _ws.send("value" + ele.uuId + " i : " + ele.value);
  }
}

function ui_create_number( parent_ele, d )
{
  var ele = ui_create_ctl( parent_ele, "input", d.title, d );
  
  if( ele != null )
  {
    ele.value     = d.value;
    ele.maxValue  = d.max;
    ele.minValue  = d.min;
    ele.stepValue = d.step;
    ele.decpl     = d.decpl;
    ele.addEventListener('keyup', ui_number_keyup );
  }
  
}

function ui_set_progress( ele_id, value )
{
  var ele = dom_id_to_ele(ele_id);

  ele.value = Math.round( ele.max * (value - ele.minValue) / (ele.maxValue - ele.minValue));
}

function ui_create_progress( parent_ele, d )
{
  var ele = ui_create_ctl( parent_ele, "progress", d.title, d );

  if( ele != null )
  {
    ele.max      = 100;
    ele.maxValue = d.max;
    ele.minValue = d.min;
    ui_set_progress( ele.id, d.value );
  }
}


function ui_create( parentId, ele_type, d )
{
  var parent_ele  = ui_get_parent(parentId);
  
  if( parent_ele != null )
  {
    switch( ele_type )
    {
      case "div":
      ui_create_div( parent_ele, d )
      break;

      case "title":
      ui_create_title( parent_ele, d )
      break;

      case "button":
      ui_create_button( parent_ele, d )
      break;

      case "check":
      ui_create_check( parent_ele, d )
      break;

      case "select":
      ui_create_select( parent_ele, d );
      break;

      case "option":
      ui_create_option( parent_ele, d );
      break;

      case "string":
      ui_create_string( parent_ele, d );
      break;

      case "number":
      ui_create_number( parent_ele, d );
      break;

      case "progress":
      ui_create_progress( parent_ele, d );
      break;
      
      default:
      ui_error("Unknown UI element type: " + ele_type )
    }
  }
}


function ws_send( d )
{
  s = JSON.stringify(d)
  //console.log(s)
  _ws.send(s)
}

function ws_on_msg( jsonMsg )
{
  //console.log(jsonMsg)
  d = JSON.parse(jsonMsg.data);

  switch( d.op )
  {
    case 'create':
    for (const ele_type in d.children)
    {
      ui_create( d.parent, ele_type, d.value[ele_type] )
      //console.log(`${ele_type}: ${d.value[ele_type]}`);
      
    }
    
    break;
  }

}

function ws_on_open()
{
  set_app_title( "Connected", "title_connected" );
  _ws.send("init")
}

function ws_on_close()
{
  set_app_title( "Disconnected", "title_disconnected" );
}

function main()
{
  _ws = new WebSocket("ws://127.0.0.1:5687/","ui_protocol")
  
  _ws.onmessage    = ws_on_msg
  _ws.onopen       = ws_on_open 
  _ws.onclose      = ws_on_close; 
}

