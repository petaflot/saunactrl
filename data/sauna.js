// sauna.js - controls WebSocket-driven UI, exponential decay, click handlers

// WebSocket
let ws;

// timestamps
let lastUpdate = 0;         // overall last device update (used for power color/size decay)
let lastTempUpdate = 0;     // last time temperature value updated (opacity decay)
let lastTargetUpdate = 0;   // last time target was updated (used for target shrink reset)
let lastPIDUpdate = 0;      // last time PID value updated (opacity)
const lastPillUpdate = [0,0,0]; // per-relay pill reset times

// modes & last values
let mode = "modeUnknown";
let lastTempValue = null;
let timerEnable = false;

// Configurable independent half-lives (seconds)
const halfLifeColor = 7;    // power/relay color saturation half-life
const halfLifeSize  = 12;   // power/relay knob size half-life
const halfLifePill  = 6;    // pill indicator shrink half-life (per-relay)
const halfLifeAlpha  = 15;  // currentTemp opacity half-life
const halfLifePIDAlpha = 5; // PID opacity half-life
const halfLifeTargetSize = 20; // target font size half-life
const reloadTimeout = 30;   // seconds before power-toggle switches to reload mode

// Colors/sizes
const hueOn = 120, hueOff = 0, huePID = 240;
const sat0 = 100;
const size0 = 26;   // initial knob diameter in px
const sizeMin = 8;
const nominalKnob = 30; // nominal CSS knob diameter used for anchor offsets
const pillScale0 = 1.0; // pill initial scale (1 == normal)
const pillScaleMin = 0.2;
const lightness = 50;

// anchors for relay knob centers (px)
const relayAnchors = [5,45,85];

// utility exponential decay
function expDecay(val0, t, halfLife, min=0){
  if (halfLife <= 0) return Math.max(val0, min);
  const k = Math.log(2)/halfLife;
  const v = val0 * Math.exp(-k * t);
  return Math.max(v, min);
}

function updateTimer(){
  var now = new Date();
  clock = document.getElementById("clock-hm")
  clock.textContent = `${now.getHours().toString().padStart(2,'0')}:${now.getMinutes().toString().padStart(2,'0')}`;
  //const t = (now - lastUpdate)/1000;

  if (timerEnable) {
    var interval = countDownDate - now;
    if (interval>0) {
      var hours = Math.floor((interval % (1000 * 60 * 60 * 24)) / (1000 * 60 * 60));
      var minutes = Math.floor((interval % (1000 * 60 * 60)) / (1000 * 60));
      var seconds = Math.floor((interval % (1000 * 60)) / 1000);

      document.getElementById("timer-hm").innerHTML = "-" + hours.toString().padStart(2,'0') + ":" + minutes.toString().padStart(2,'0');// + ":"; + seconds.toString().padStart(2,'0');

      document.getElementById("timer-hm").style.color = "#bb7722ff";
      document.getElementById("timer-s").style.color = "#bb7722ff";
    } else {
      var hours = Math.floor((-interval % (1000 * 60 * 60 * 24)) / (1000 * 60 * 60));
      var minutes = Math.floor((-interval % (1000 * 60 * 60)) / (1000 * 60));
      var seconds = Math.floor((-interval % (1000 * 60)) / 1000);

      document.getElementById("timer-hm").innerHTML = "+" + hours.toString().padStart(2,'0') + ":" + minutes.toString().padStart(2,'0');// + ":"; + seconds;

      document.getElementById("timer-hm").style.color = "#000000";
      document.getElementById("timer-s").style.color = "#000000";
    }
  }

  document.getElementById("timer-s").innerHTML = ":" + seconds.toString().padStart(2,'0');

}

// compute and apply visuals per tick
function updateVisuals(){
  const now = Date.now();
  const t = (now - lastUpdate)/1000;
  const sat = expDecay(sat0, t, halfLifeColor);
  const size = expDecay(size0, t, halfLifeSize, sizeMin);

  // --- Power switch ---
  const slider = document.querySelector("#powerWrap .slider");
  const knobPower = document.querySelector("#powerWrap .knobPower");
  const svg = document.getElementById("powerReloadSVG");
  if (slider && knobPower){
    const age = (now - lastUpdate)/1000;
    const reloadActive = age > reloadTimeout;

    if (reloadActive){
      knobPower.style.display = "none";
      if (svg) {
        svg.style.display = "block";
        const svgSize = size0;
        svg.setAttribute("width", svgSize);
        svg.setAttribute("height", svgSize);
        svg.style.width = svgSize + "px";
        svg.style.height = svgSize + "px";
        svg.style.animation = "spinccw 1.4s linear infinite";
      }
      slider.style.backgroundColor = "hsl(0,0%,50%)";
    } else {
      if (svg) { svg.style.display = "none"; svg.style.animation = ""; }
      knobPower.style.display = "block";
      let hue = mode.startsWith("on") ? hueOn : hueOff;
      if (mode === "modeUnknown") slider.style.backgroundColor = "hsl(0,0%,50%)";
      else slider.style.backgroundColor = `hsl(${hue}, ${sat}%, ${lightness}%)`;
      knobPower.style.width = knobPower.style.height = size + "px";
      // center knob vertically within rail:
      const railHeight = parseFloat(getComputedStyle(slider).height) || 34;
      knobPower.style.top = (railHeight-size)/2 + "px";

      const railWidth = parseFloat(getComputedStyle(slider).width) || 60;
      knobPower.style.left = (railWidth-size)/2 + "px";
      //const translateX = (mode.startsWith("on")) ? (railWidth-nominalKnob - 8) : -8;
      const translateX = (mode.startsWith("off")) ? -(railWidth-nominalKnob-4)/2 : (railWidth-nominalKnob-4)/2;
      knobPower.style.transform = `translateX(${translateX}px)`;
    }
  }

  // --- Relays ---
  for (let i=1;i<=3;i++){
    const el = document.getElementById("relay"+i);
    if (!el) continue;
    let hue;
    if (el.classList.contains("on")) hue = hueOn;
    else if (el.classList.contains("pid")) hue = huePID;
    else if (el.classList.contains("off")) hue = hueOff;
    else { el.style.backgroundColor = "hsl(0,0%,50%)"; continue; }
    el.style.backgroundColor = `hsl(${hue}, ${sat}%, ${lightness}%)`;

    const knob = el.querySelector(".knob");
    if (!knob) continue;
    knob.style.width = knob.style.height = size + "px";
    // center knob at anchor point
    let anchorTop = relayAnchors[1];
    if (el.classList.contains("on")) anchorTop = relayAnchors[0];
    else if (el.classList.contains("pid")) anchorTop = relayAnchors[1];
    else if (el.classList.contains("off")) anchorTop = relayAnchors[2];
    knob.style.top = (anchorTop + (nominalKnob - size) / 2) + "px";
    knob.style.left = ((40 - size)/2) + "px";

    // pill shrink independent half-life (per-relay)
    const pill = knob.querySelector(".pill");
    const pillElapsed = (Date.now()-lastPillUpdate[i-1])/1000;
    const pillScale = expDecay(pillScale0, pillElapsed, halfLifePill, pillScaleMin);
    if (pill) pill.style.transform = `translate(-50%,-50%) scale(${pillScale})`;

    // error X visibility: show/hide independently of pill scale
    const err = knob.querySelector(".err-x");
    if (el.classList.contains("relayError")) {
      if (err) err.style.display="block";
      if (pill) pill.style.display="none";
    } else {
      if (err) err.style.display="none";
      if (pill) pill.style.display="block";
    }
  }

  // --- Current Temp ---
  const currentTempEl=document.getElementById("currentTemp");
  if (lastTempValue===-127.0){
    currentTempEl.textContent="Temp. Error";
    currentTempEl.className="tempError";
    currentTempEl.style.opacity=1;
  } else if (lastTempValue!==null){
    const tAlpha=(Date.now()-lastTempUpdate)/1000;
    const alpha=expDecay(1.0,tAlpha,halfLifeAlpha,0.05);
    currentTempEl.style.opacity=alpha;
    currentTempEl.textContent=lastTempValue.toFixed(1)+"°C";
    currentTempEl.className="currentTemp";
  }

  // --- Target Temp shrinking ---
  const targetEl=document.getElementById("targetTempDisplay");
  if (targetEl){
    const tTarget=(Date.now()-lastTargetUpdate)/1000;
    const sizeEmMax=4, sizeEmMin=2;
    const sizeEm=Math.max(sizeEmMin,expDecay(sizeEmMax,tTarget,halfLifeTargetSize,sizeEmMin));
    targetEl.style.fontSize=sizeEm+"em";
    // centering is handled by container (.targetContainer)
  }

  // --- PID fade ---
  const pidEl=document.getElementById("valueOfPID");
  if (pidEl){
    const tPID=(Date.now()-lastPIDUpdate)/1000;
    const alphaPID=expDecay(1.0,tPID,halfLifePIDAlpha,0.1);
    pidEl.style.opacity=alphaPID;
  }
}

function setMode(newMode){
  mode=newMode;
  const statusEl=document.getElementById("status");
  const toggleWrap=document.getElementById("powerWrap");
  toggleWrap.className="switch "+mode;
  statusEl.textContent=(mode==="on")?"On":(mode==="off")?"Off":"Unknown";
  updateVisuals();
}

function setRelayUI(relay,mode,state="OFF"){
  const el=document.getElementById("relay"+relay);
  const status=document.getElementById("relay"+relay+"Status");
  el.className="v-switch "+mode;
  el.classList.remove("relayOn","relayError");
  const knob=el.querySelector(".knob");
  const pill=knob?knob.querySelector(".pill"):null;
  if (state==="ON"){ el.classList.add("relayOn"); if (pill) lastPillUpdate[relay-1]=Date.now(); }
  else if (state==="ERROR"){ el.classList.add("relayError"); }
  status.textContent=(mode==="on")?"On":(mode==="pid")?"PID":(mode==="off")?"Off":"Unknown";
}

function clickRelay(event,relay){
  const el=document.getElementById("relay"+relay);
  const rect=el.getBoundingClientRect();
  const y=event.clientY-rect.top;
  const third=rect.height/3;
  const newMode=(y<third)?"on":(y<2*third)?"pid":"off";
  setRelayUI(relay,newMode);
  if (ws && ws.readyState===WebSocket.OPEN) ws.send("relay:"+relay+":"+newMode);
}

function handlePowerToggle(ev){
  const now=Date.now();
  const age=(now-lastUpdate)/1000;
  if (age>reloadTimeout){ location.reload(); }
  else {
    if (ev.target.checked){ if (ws && ws.readyState===WebSocket.OPEN) ws.send("enable"); setMode("on"); }
    else { if (ws && ws.readyState===WebSocket.OPEN) ws.send("disable"); setMode("off"); }
  }
}

window.onload=function(){
  // open socket
  ws=new WebSocket("ws://"+window.location.host+"/ws");
  ws.onmessage=(event)=>{
    try{
      const data=JSON.parse(event.data);
      lastUpdate=Date.now();
      lastTempUpdate=Date.now();
      lastTargetUpdate=Date.now();
      lastPIDUpdate=Date.now();
      lastTempValue=data.temp;

      const targetEl=document.getElementById("targetTempDisplay");
      if (targetEl) targetEl.textContent=data.target.toFixed(1)+"°C";

      const pidText=document.getElementById("valueOfPID");
      if (pidText) pidText.textContent="PID: "+(data.pid!==undefined?data.pid.toFixed(1):"--");

      const modeMap=["off","pid","on"];
      const stateMap=["OFF","ON","ERROR"];
      for (let i=0;i<3;i++){
        const m=data.relayModes?(modeMap[data.relayModes[i]]||"modeUnknown"):"modeUnknown";
        const st=data.relayStates?(stateMap[data.relayStates[i]]||"OFF"):"OFF";
        setRelayUI(i+1,m,st);
      }

      if (data.enabled) {
          setMode("on");
	  //timerEnable = true;
	} else {
	  setMode("off");
	  timerEnable = false;
	}
    } catch(e){ console.error("Parse error:",e); }
  };

  const powerToggle = document.getElementById("powerToggle");
  if (powerToggle) powerToggle.onchange=handlePowerToggle;

  const targetTempDisplay = document.getElementById("targetTempDisplay");
  if (targetTempDisplay) {
    targetTempDisplay.onclick=()=>{
      const val=prompt("Enter new target temperature (°C):","");
      if (val!==null){
        if (ws && ws.readyState===WebSocket.OPEN) ws.send("target:"+val);
        lastTargetUpdate=Date.now();
      }
    };
  }

  const timeDisplay_hm = document.getElementById("timer-hm");
  const timeDisplay_s = document.getElementById("timer-s");
  if (timeDisplay_hm) {
    timeDisplay_hm.onclick=()=>{
      const val=prompt("Enter new timer value in minutes:","");
      if (val!==null){
	//if (val=="") {
	//  timerEnable = true;
        //} else 
	if (val>0) {
	  timerEnable = true;
  	  countDownDate = new Date().getTime()+val*60000;
	//} else {
	//  timerEnable = false;
  	//  countDownDate = new Date().getTime()-val*60000;
	}
        updateTimer();
      }
    };
  }

  setInterval(updateVisuals,500);
  setInterval(updateTimer,1000);
  setMode("modeUnknown");
};

