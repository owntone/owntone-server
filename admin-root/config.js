Event.observe(window,'load',init);
//###TODO
// * Fix cancel for multiple, create a span holding all multiple elements
// * Disable/enable save/cancel, Add key and onchange listeners keep a note on changed items
// * create the path/file browser
// * Inform user if server restart needed, ask Ron which options
// * Show required options in UI check with Ron if conf.c is correct
// * better errormessage for not writable config
// * make tabs?
// * add warning if leaving page without saving
      
// Config isn't defined until after the Event.observe above
// I could have put it below Config = ... but I want all window.load events
// at the start of the file

function init() {
  Config.init();
}
var ConfigXML = {
  config: {},
  configIndex: {},
  getOptionFromElementName: function (name) {
    return this.configIndex[name];
  },
  getOption: function (section,id) {
    return this.config[section][id];
  },
  getSections: function () {
    return $H(this.config).keys();
  },
  getItems: function(section) {
    return $H(this.config[section]).keys();
  },
  parseXML: function(xmlDoc) {
    $A(xmlDoc.getElementsByTagName('section')).each(function (section) {
      var items = {};
      $A(section.getElementsByTagName('item')).each(function (item) {
        var returnItem = {};
        $A(item.attributes).each(function (attr) {
          returnItem[attr.name] = attr.value;  
        });
        $A(item.childNodes).each(function (node) {
          if (Element.textContent(node) == '') {
            return;
          }
          if ('options' == node.nodeName) {
            var options = [];
            $A(item.getElementsByTagName('option')).each(function (option) {
              options.push({value: option.getAttribute('value'),
                            label: Element.textContent(option)});
            });
            returnItem['options'] = options;     
          } else {
            returnItem[node.nodeName] = Element.textContent(node);
          }
          $A(node.attributes).each(function (attr) {
            returnItem[attr.name] = attr.value;  
          });
        });
        // Double index everything one as [section][id]
        // and one as [config_section:id]
        items[item.getAttribute('id')] = returnItem;
        ConfigXML.configIndex[returnItem.config_section+ ':' +returnItem.id] = returnItem;
      });
      ConfigXML.config[section.getAttribute('name')] = items;
    });
  }
};
var ConfigInitialValues = {
  values: {},
  getValues: function () {
    return $H(ConfigInitialValues.values);
  },
  getValue: function (section,id) {
    return ConfigInitialValues.values[section+':'+id];
  },
  parseXML: function (xmldoc) {
    // IE and w3c treat xmldoc differently make shore firstChild is firstchild of <config>
    if (xmldoc.childNodes[1] && xmldoc.childNodes[1].nodeName == 'config') {
      sections = $A(xmldoc.childNodes[1].childNodes);
    } else {  
      sections = $A(xmldoc.firstChild.childNodes);
    }
    sections.each(function (section) {
      var sectionName = section.nodeName;
      $A(section.childNodes).each(function (node) {
        if (node.firstChild && node.firstChild.hasChildNodes()) {
          var values = [];
          $A(node.childNodes).each(function (n) {
            values.push(Element.textContent(n));
          });
          ConfigInitialValues.values[sectionName+':'+node.nodeName] = values;
        } else {
          ConfigInitialValues.values[sectionName+':'+node.nodeName] = Element.textContent(node);
        }
      });
    });
  } 
};
var Config = {
  configPath: '',
  configOptionValues: '',
  init: function () {
    new Ajax.Request('/config.xml',{method: 'get',onComplete: Config.storeConfigLayout});
  },
  storeConfigLayout: function (request) {
    ConfigXML.parseXML(request.responseXML);
    new Ajax.Request('/xml-rpc?method=stats',{method: 'get',onComplete: Config.updateStatus});
  },
  updateStatus: function (request) {
    Config.configPath = Element.textContent(request.responseXML.getElementsByTagName('config_path')[0]);
    Config.isWritable = Element.textContent(request.responseXML.getElementsByTagName('writable_config')[0]) == '1';
//    $('config_path').appendChild(document.createTextNode(
      
//    );
    new Ajax.Request('/xml-rpc?method=config',{method: 'get',onComplete: Config.showConfig});  
  },
  showConfig: function (request) {
    ConfigInitialValues.parseXML(request.responseXML);
    Config.configOptionValues = request.responseXML;
    var sections = ConfigXML.getSections();
    sections.each(function (section) {
      var head = document.createElement('div');
      head.className= 'naviheader';
      head.appendChild(document.createTextNode(section));
      var body = document.createElement('div');
      body.className = 'navibox';
      if ('Server' == section) {
        body.appendChild(Builder.node('span',{id:'config_path'},'Config File'));
        body.appendChild(document.createTextNode(Config.configPath));
        body.appendChild(Builder.node('br'));
        body.appendChild(Builder.node('div',{style: 'clear: both;'}));
      }
      ConfigXML.getItems(section).each(function (itemId) {
        body.appendChild(Config._buildItem(section,itemId));
      });
      $('theform').appendChild(head);
      $('theform').appendChild(body);
    });
    if (!Config.isWritable) {
      Effect.Appear('config_not_writable_warning');
    } else {
      // Create save and cancel buttons
//      var save = Builder.node('button',{id: 'button_save', disabled: 'disabled'},'Save');
      var save = Builder.node('button',{id: 'button_save'},'Save');
      Event.observe(save,'click',saveForm);
      var cancel = Builder.node('button',{id: 'button_cancel'},'Cancel');
      Event.observe(cancel,'click',cancelForm);
      var spacer = document.createTextNode('\u00a0\u00a0');
      var buttons = $('buttons');
      if (navigator.platform.toLowerCase().indexOf('mac') != -1) {
        // We're on mac
        buttons.appendChild(cancel);
        buttons.appendChild(spacer);
        buttons.appendChild(save);
      } else {
        //###TODO What about all them unix variants?
        buttons.appendChild(save);
        buttons.appendChild(spacer);
        buttons.appendChild(cancel);
      }
    }
  },
  _buildItem: function(section,itemId) {
    var frag = document.createDocumentFragment();
    var href;
    var span;
    var item = ConfigXML.getOption(section,itemId);
    var postId = item.config_section + ':' + itemId;
    var noBrowse = false;
    switch(item.type) {
      case 'text':
        if (item.multiple) {
          var values = ConfigInitialValues.getValue(item.config_section,item.id);
          if (!values || values.length == 0) {
            values = [''];
          }
          var parentSpan = Builder.node('span');
          values.each(function (val,i) {
            var span = document.createElement('span');
            span.appendChild(BuildElement.input(postId+i,postId,
                                     item.name,
                                     val || item.default_value || '',
                                     item.size || 20,
                                     item.short_description,                                     
                                     ''));
            if (item.browse) {
              href = Builder.node('a',{href: 'javascript://'},'Browse');
              Event.observe(href,'click',Config._browse);          
              span.appendChild(href);
            }
            span.appendChild(document.createTextNode('\u00a0\u00a0'));
            href = Builder.node('a',{href: 'javascript://'},'Remove');
            Event.observe(href,'click',Config._removeItemEvent);
            span.appendChild(href);
            span.appendChild(Builder.node('br'));
            parentSpan.appendChild(span);
          });
          // This is used by cancelForm to find out how
          // many options a multiple group has
          frag.appendChild(parentSpan);
          href = Builder.node('a',{href:'javascript://',className:'addItemHref'},item.add_item_label);
          frag.appendChild(href);
          Event.observe(href,'click',Config._addItemEvent);
          frag.appendChild(Builder.node('div',{style:'clear: both'}));
        } else {
          frag.appendChild(BuildElement.input(postId,postId,
                                   item.name,
                                   ConfigInitialValues.getValue(item.config_section,item.id) || item.default_value || '',
                                   item.size || 20,
                                   item.short_description,                                     
                                   ''));
          if (item.browse) {
            href = Builder.node('a',{href: 'javascript://'},'Browse');
            Event.observe(href,'click',Config._browse);          
            frag.appendChild(href);
          }
          frag.appendChild(Builder.node('br'));
        }
        break;
      case 'select':
        frag.appendChild(BuildElement.select(postId,
                                  item.name,
                                  item.options,
                                  ConfigInitialValues.getValue(item.config_section,item.id) || item.default_value,
                                  item.short_description));
        frag.appendChild(Builder.node('br'));
        break;
    }
    return frag;
  },
  _addItemEvent: function (e) {
    var span = Event.element(e).previousSibling.lastChild;
    Config._addItem(span);      
  },
  _addItem: function(span) {
    var newSpan = span.cloneNode(true);
    var id = newSpan.getElementsByTagName('input')[0].id;
    var num = parseInt(id.match(/\d+$/));
    num++;
    var id = id.replace(/\d+$/,'') + num;

    newSpan.getElementsByTagName('label')[0].setAttribute('for',id);
    newSpan.getElementsByTagName('input')[0].id = id;
    newSpan.getElementsByTagName('input')[0].value = '';
    var hrefs = newSpan.getElementsByTagName('a');
    if ('Netscape' == navigator.appName) {
      // Firefox et al doesn't copy registered events on an element deep clone
      // Don't know if that is w3c or if IE has it right
      if (hrefs.length == 1) {
        Event.observe(hrefs[0],'click',Config._removeItemEvent);
      } else {
        Event.observe(hrefs[0],'click',Config._browse);          
        Event.observe(hrefs[1],'click',Config._removeItemEvent);        
      }
    }
    span.parentNode.insertBefore(newSpan,span.nextSibling);
  },
  _removeItemEvent: function (e) {
    var span = Event.element(e);
    while (span.nodeName.toLowerCase() != 'span') {
      span = span.parentNode;
    }
    Config._removeItem(span);
  },
  _removeItem: function(span) {
    if (span.parentNode.childNodes.length > 1) {
      Element.remove(span);
    } else {
      span.getElementsByTagName('input')[0].value='';
    }
  },
  _browse: function(e) {
    alert('Browse');
  }
}
var BuildElement = {
  input: function(id,name,displayName,value,size,short_description,long_description) {

    var frag = document.createDocumentFragment();
    var label = document.createElement('label');

    label.setAttribute('for',id);
    label.appendChild(document.createTextNode(displayName));
    frag.appendChild(label);
    if (Config.isWritable) {
      frag.appendChild(Builder.node('input',{id: id,name: name,className: 'text',
                                             value: value,size: size}));
    } else {
      frag.appendChild(Builder.node('input',{id: id,name: name,className: 'text',
                                             value: value,size: size, disabled: 'disabled'}));
    }
    frag.appendChild(document.createTextNode('\u00a0'));
    if (short_description) {                                              
      frag.appendChild(document.createTextNode(short_description));
    }

    return frag;
  },
  select: function(id,displayName,options,value,short_description,long_description) {
    var frag = document.createDocumentFragment();
    var label = document.createElement('label');
    label.setAttribute('for',id);
    label.appendChild(document.createTextNode(displayName));
    frag.appendChild(label);
    
    var select = Builder.node('select',{id: id,name: id,size: 1});
    if (!Config.isWritable) {
      select.disabled = 'disabled';
    }
    $A(options).each(function (option) {
      select.appendChild(Builder.node('option',{value: option.value},
                                                option.label));
    });
    select.value = value;
    frag.appendChild(select);
    frag.appendChild(document.createTextNode('\u00a0'));
    if (short_description) {
      frag.appendChild(document.createTextNode(short_description));
    }

    return frag;
  }
    
}
function saved(req) {
  if ('200' == Element.textContent(req.responseXML.getElementsByTagName('status')[0])) {
    alert('Saved');
  } else {
    alert("Couldn't save and if this weren't a beta I'd tell you why");
  }
}
function saveForm() {
  var postVars = [];
  var multiple = {};
  $A($('theform').getElementsByTagName('select')).each(function (select) {
    postVars.push(Form.Element.serialize(select.id));
  });
  $A($('theform').getElementsByTagName('input')).each(function (input) {
    if (ConfigXML.getOptionFromElementName(input.name).multiple) {
      if (multiple[input.name]) {
        multiple[input.name].push(encodeURIComponent(input.value));
      } else {
        multiple[input.name] = [input.value];
      }
    } else {
      postVars.push(Form.Element.serialize(input.id));       
    };
  });
  $H(multiple).each(function (item) {
    postVars.push(item.key + '=' + item.value.join(','));
  });
  new Ajax.Request('/xml-rpc?method=updateconfig',
                   {method: 'post',
                parameters: postVars.join('&'),
                onComplete: saved});
}
function cancelForm() {
  ConfigXML.getSections().each(function (section){
    ConfigXML.getItems(section).each(function (itemId) {
      var item = ConfigXML.getOption(section,itemId);
      var itemConfigId = item.config_section + ':' + item.id;
      if (item.multiple) {
        var values = ConfigInitialValues.getValue(item.config_section,itemId);
        if (!values || values.length == 0) {
          values = [''];
        }
        var initialValuesCount = values.length;
        var currentElements = document.getElementsByName(itemConfigId);
var i=0;
        while (initialValuesCount < currentElements.length) {
          i++;
          if (i > 10) {
              alert('Getting dizy; too many turns in this loop (silly errormessage 1');
              return;
          }
          Config._removeItem(currentElements[0].parentNode);
       }
        while (initialValuesCount > currentElements.length) {
          i++;
          if (i > 10) {
              alert('An important part came off (silly errormessage 2');
              return;
          }
          Config._addItem(currentElements[currentElements.length-1].parentNode);
        }
        values.each(function (val,i){
          currentElements[i].value = val;
        });
      } else {
          //###TODO potential error a select without a default value
        $(itemConfigId).value = ConfigInitialValues.getValue(item.config_section,item.id) || item.default_value || '';
      }
    });
  });
  return;
  ConfigInitialValues.getValues().each(function (option) {
    if (typeof (option.value) == 'object') {
        //###TODO what if user removed one of the multiplevalued options?
      if (option.value != '') {  
        option.value.each(function (val,i) {
          $(option.key + i).value = val;
        });   
      }
    }
    var el = $(option.key);
    if (el) {
      el.value = option.value;
    }
  });
}
Object.extend(Element, {
  removeChildren: function(element) {
  while(element.hasChildNodes()) {
    element.removeChild(element.firstChild);
  }
  },
  textContent: function(node) {
  if ((!node) || !node.hasChildNodes()) {
    // Empty text node
    return '';
  } else {
    if (node.textContent) {
    // W3C ?
    return node.textContent;
    } else if (node.text) {
    // IE
    return node.text;
    }
  }
  // We shouldn't end up here;
  return '';
  }
});
/* Detta script finns att hamta pa http://www.jojoxx.net och 
   far anvandas fritt sa lange som dessa rader star kvar. */ 
function DataDumper(obj,n,prefix){
	var str=""; prefix=(prefix)?prefix:""; n=(n)?n+1:1; var ind=""; for(var i=0;i<n;i++){ ind+="  "; }
	if(typeof(obj)=="string"){
		str+=ind+prefix+"String:\""+obj+"\"\n";
	} else if(typeof(obj)=="number"){
		str+=ind+prefix+"Number:"+obj+"\n";
	} else if(typeof(obj)=="function"){
		str+=ind+prefix+"Function:"+obj+"\n";
	} else if(typeof(obj) == 'boolean') {
       str+=ind+prefix+"Boolean:" + obj + "\n";
	} else {
		var type="Array";
		for(var i in obj){ type=(type=="Array"&&i==parseInt(i))?"Array":"Object"; }
		str+=ind+prefix+type+"[\n";
		if(type=="Array"){
			for(var i in obj){ str+=DataDumper(obj[i],n,i+"=>"); }
		} else {
			for(var i in obj){ str+=DataDumper(obj[i],n,i+"=>"); }
		}
		str+=ind+"]\n";
	}
	return str;
}
