var MOUSE_DOWN = 1, MOUSE_UP = 2, MOUSE_MOVE = 3, KEY = 4,
    MOUSE_WHEEL = 5;

var sendBuffer = new Int32Array(4);   
var mouseDown = false;
//var count = 4; //use this to send only 1/4 of the move events, makes it
               //more responsive
var TOUCH_EVENTS = false;
// function getPos(e) {
//   e = e || window.event;
//   var docEl = document.documentElement;
//   var scrollLeft = docEl.scrollLeft || document.body.scrollLeft;
//   var scrollTop  = docEl.scrollTop || document.body.scrollTop;
//   x = e.pageX || (e.clientX  + scrollLeft);
//   y = e.pageY || (e.clientY  + scrollTop);
// }​

function sendMouseEvent(m, e) {
  //e.preventDefault();
  //if(m == MOUSE_MOVE) if(--count > 0) return;
  e = e || window.event;
  sendBuffer[0] = m;
  sendBuffer[1] = e.clientX;
  sendBuffer[2] = e.clientY;
  var b = 0;
  if(e.button == 0) b = 0;
  else if(e.button == 1) b = 1;
  else if(e.button == 2) b = 4;
  sendBuffer[3] = b;

  // if(!websocket || websocket.readyState != websocket.open) {
  //   console.log(sendBuffer[0] + " " +
  //               sendBuffer[1] + " " +
  //               sendBuffer[1]);
  //   //return;              
  // }  
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

window.onkeydown = sendKeyEvent;

//window.onkeypress = sendKeyEvent;

window.onmousedown = function(e) {
  e.preventDefault();
  mouseDown = true;
  sendMouseEvent(MOUSE_DOWN, e);                    
}
window.onmouseup = function(e) {
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

window.onmousewheel = MouseWheelHandler;

window.contextmenu = window.onmousedown;

function handleStart(e) {
  //document.querySelector("#pos").textContent = e.touches.length;
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


