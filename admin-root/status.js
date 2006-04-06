Event.observe(window,'load',initStatus);

var UPDATE_FREQUENCY = 5; // number of seconds between updates

function initStatus(e) {
  Event.observe('update','keyup',Updater.keyUp);
  Updater.start();
}
var Updater = {
  start: function () {
    if (f = Cookie.getVar('update_frequency')) {
      this.frequency = f;
    } else {
      this.frequency = UPDATE_FREQUENCY;
    }
    $('update').value = this.frequency;
    new Ajax.Request('xml-rpc?method=stats',{method: 'get',onComplete: rsStats});          
  },
  update: function () {
    $('update_timer').style.width = 100 + 'px';
    if (Updater.stop) {
      return;
    }
    Updater.effect = new Effect.Scale('update_timer',0,{scaleY: false,duration: Updater.frequency,
      afterFinish: function() {
        new Ajax.Request('xml-rpc?method=stats',{method: 'get',onComplete: rsStats});          
      }});
  },
  keyUp: function (e) {
    var val = $('update').value;
    if (Updater.oldVal == val) {
      return;
    }
    Updater.oldVal = val;
    if (Updater.effect) {
      Updater.effect.cancel();
    }
    if (('' == val) || /^\d+$/.test(val)) {
      Cookie.setVar('update_frequency',val,30);
      if ('' == val) {
        Element.removeClassName('update','input_error');
        return;
      }
      Updater.frequency = val;
      Element.removeClassName('update','input_error');
      Updater.update();
    } else {
      Element.addClassName('update','input_error');
    }
  } 
}
function rsStats(request) {
  ['service','stat'].each(function (tag) {
    $A(request.responseXML.getElementsByTagName(tag)).each(function (element) {
      var node = $(element.firstChild.firstChild.nodeValue.toLowerCase().replace(/\ /,'_'));  
      node.replaceChild(document.createTextNode(element.childNodes[1].firstChild.nodeValue),node.firstChild);
    });
  });  
  var thread = $A(request.responseXML.getElementsByTagName('thread'));
  var threadTable = new Table('thread');
  threadTable.removeTBodyRows();
  thread.each(function (element) {
    row = [];
    row.push(element.childNodes[1].firstChild.nodeValue);
    row.push(element.childNodes[2].firstChild.nodeValue);
    threadTable.addTbodyRow(row);    
  });
  Updater.update();
}

Table = Class.create();
Object.extend(Table.prototype,{
  initialize: function (id) {
    this.tbody = $(id).getElementsByTagName('tbody')[0];  
  },
  removeTBodyRows: function () {
    Element.removeChildren(this.tbody);
  },
  addTbodyRow: function (cells) {
    var tr = document.createElement('tr');
    cells.each(function (cell) {
      var td = document.createElement('td');
      td.appendChild(document.createTextNode(cell));
      tr.appendChild(td);
    });
    this.tbody.appendChild(tr);
  }
});
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
var Cookie = {
  getVar: function(name) {
    var cookie = document.cookie;
    if (cookie.length > 0) {
      cookie += ';';
    }
    re = new RegExp(name + '\=(.*?);' );
    if (cookie.match(re)) {
      return RegExp.$1;
    } else {
      return '';
    }
  },
  setVar: function(name,value,days) {
    days = days || 30;
    var expire = new Date(new Date().getTime() + days*86400);
    document.cookie = name + '=' + value +';expires=' + expire.toUTCString();
  },
  removeVar: function(name) {
    var date = new Date(12);
    document.cookie = name + '=;expires=' + date.toUTCString();
  }
};
