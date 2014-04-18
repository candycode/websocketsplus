function initEvents(gui, websocket) {

var MOUSE_DOWN = 1, MOUSE_UP = 2, MOUSE_MOVE = 3, KEY = 4,
    MOUSE_WHEEL = 5, RESIZE = 6, TEXT = 7;

var sendBuffer = new Int32Array(4);
var textBuffer = new Int32Array(16);   
var mouseDown = false;
//var count = 4; //use this to send only 1/4 of the move events, makes it
               //more responsive
var TOUCH_EVENTS = false;

function getPos(e) {
  var e = window.e || e;
  var rect = gui.getBoundingClientRect();
  return {x: e.clientX - rect.left,
          y: e.clientY - rect.top};
  
}

function sendMouseEvent(m, e) {
  //e.preventDefault();
  //if(m == MOUSE_MOVE) if(--count > 0) return;
  e = e || window.event;
  var p = getPos(e);
  sendBuffer[0] = m;
  sendBuffer[1] = p.x;
  sendBuffer[2] = p.y;
  var b = 0;
  if(e.button == 0) b = 0;
  else if(e.button == 1) b = 1;
  else if(e.button == 2) b = 4;
  sendBuffer[3] = b;
  websocket.send(sendBuffer);
  //if(m == MOUSE_MOVE) count = 4;
}

//ALL Chars are sent as capital letters!
//http://www.javascriptkit.com/jsref/eventkeyboardmouse.shtml
function sendKeyEvent(e) {
  //alert(e.keyCode);
  e = window.event || e;
  sendBuffer[0] = KEY;
  sendBuffer[1] = e.keyCode || e.charCode;
  var k = 0;
  if(e.altKey) k |= 0x1;
  if(e.ctrlKey) k |= 0x10;
  if(e.shiftKey) k != 0x100;
  sendBuffer[2] = k;
  websocket.send(sendBuffer);
}

window.sendResizeEvent = function(w, h) {
  sendBuffer[0] = RESIZE;
  sendBuffer[1] = w;
  sendBuffer[2] = h;
  websocket.send(sendBuffer);
}

window.onkeydown = sendKeyEvent;

//window.onkeypress = sendKeyEvent;

function valid(e) {
  var rect = gui.getBoundingClientRect();
  return (window.e || e) 
         && e.clientX < rect.right && e.clientX > rect.left  
         && e.clientY > rect.top && e.clientY < rect.bottom;  
} 


window.onmousedown = function(e) {
  if(!valid(e)) return true;
  e.preventDefault();
  mouseDown = true;
  sendMouseEvent(MOUSE_DOWN, e);                    
}
window.onmouseup = function(e) {
  if(!valid(e)) return true;
  e.preventDefault();
  mouseDown = false;
  sendMouseEvent(MOUSE_UP, e);                    
}
window.onmousemove = function(e) {
  e.preventDefault();
  if(!mouseDown) return;
  sendMouseEvent(MOUSE_MOVE, e);                    
}

function MouseWheelHandler(e) {
  // cross-browser wheel delta
  var e = window.event || e; // old IE support
  e.preventDefault();
  var delta = Math.max(-1, Math.min(1, (e.wheelDelta || -e.detail)));
  sendBuffer[0] = MOUSE_WHEEL;
  sendBuffer[1] = e.clientX;
  sendBuffer[2] = e.clientY;
  sendBuffer[3] = delta;
  websocket.send(sendBuffer);
  return false;
}


 window.sendFilePath = function(t) {
  var s = Math.floor((t.length + 1) / 2) + 2;
  if(textBuffer.length < s)
     textBuffer = new Int32Array(s);
  textBuffer[0] = TEXT;
  textBuffer[1] = t.length;
  var j = 2;
  for(var i = 0; i < t.length; i += 2) {
    textBuffer[j] = t.charCodeAt(i);
    textBuffer[j] = textBuffer[j] | (t.charCodeAt(i + 1) << 16);
    ++j;
  }
  if(Math.floor(t.length % 2) != 0) textBuffer[s - 1] 
     = t.charCodeAt(t.length - 1);
  websocket.send(textBuffer);
}

window.onmousewheel = MouseWheelHandler;

window.contextmenu = window.onmousedown;

function handleStart(e) {
  e.preventDefault();
  sendMouseEvent(MOUSE_DOWN, e.touches[e.touches.length - 1]);    
}

function handleEnd(e) {
  e.preventDefault();
  sendMouseEvent(MOUSE_UP, e.touches[e.touches.length - 1]);    
}

function handleMove(e) {
  e.preventDefault();
  sendMouseEvent(MOUSE_MOVE, e.touches[e.touches.length - 1]);    
}

function handleCancel(e) {
  e.preventDefault();  
}

window.addEventListener("mousewheel", MouseWheelHandler);
window.addEventListener("DOMMouseScroll", MouseWheelHandler);
window.addEventListener("contextmenu", window.onmousedown);

if(TOUCH_EVENTS) {
  window.addEventListener("touchstart", handleStart, false);
  window.addEventListener("touchend", handleEnd, false);
  window.addEventListener("touchcancel", handleCancel, false);
  window.addEventListener("touchleave", handleEnd, false);
  window.addEventListener("touchmove", handleMove, false);
}

}

eventScriptLoaded = true;