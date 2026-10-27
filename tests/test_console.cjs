const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
class Element {
 constructor(tag='DIV'){this.tagName=tag;this.textContent='';this.value='';this.children=[];this.dataset={};this.attributes={};this.hidden=false;this.className='';}
 append(...nodes){this.children.push(...nodes)}
 replaceChildren(...nodes){this.children=nodes}
 setAttribute(k,v){this.attributes[k]=v}
 querySelector(tag){return this.children.find(x=>x.tagName.toLowerCase()===tag)||null}
 click(){this.clicked=true}
}
function setup(){
 const elements=new Map();const get=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id)};
 const tabs=['overview','sensor','network','system','settings','firmware'].map(id=>{const e=new Element('BUTTON');e.dataset.tab=id;return e});
 const sections=tabs.map(t=>{const e=get(t.dataset.tab);e.id=t.dataset.tab;return e});
 const actions=['sensor_reset','filter_reset','wifi_reconnect','reboot','confirm_trial','debug_logs'].map(action=>{const e=new Element('BUTTON');e.dataset.action=action;return e});
 let now=1000,fail=false,reject=false;
 const config={schema:1,interval_ms:1000,timeout_ms:100,recovery_failures:3,backoff_ms:5000,stale_ms:3000,alpha:.2,offset_pa:0,orientation:1,setpoint_pa:125,hysteresis_pa:10,power_save:false};
 const status={valid:true,firmware:'test',sdk:'test',boot_id:1,revision:1,uptime_seconds:1,reset_reason:1,sample_age_seconds:.2,sensor_dp_pa:2.05,cor_dp_pa:3,processing_difference_pa:.95,temp_f:80.6,last_good:{cor_dp_pa:3,temp_c:27},config,network:{connected:true,power_save_actual:false},system:{ota_upload_enabled:false}};
 const calls=[];const context=vm.createContext({console,Blob,AbortController,URL:{createObjectURL:()=>'',revokeObjectURL:()=>{}},confirm:()=>true,setTimeout:()=>1,clearTimeout:()=>{},setInterval:()=>{},location:{hostname:'unit-device'},performance:{now:()=>now},document:{hidden:false,activeElement:new Element('BUTTON'),getElementById:get,createElement:t=>new Element(t.toUpperCase()),createElementNS:(ns,t)=>new Element(t.toUpperCase()),querySelectorAll:q=>q==='[data-tab]'?tabs:q==='main>section'?sections:actions},fetch:async(url,options)=>{calls.push({url,options});if(fail)throw Error('offline');return {ok:!reject,status:reject?401:200,json:async()=>url.endsWith('/status')?status:url.endsWith('/logs')?{entries:[{seconds:1,message:'Wi-Fi event'},{seconds:2,message:'CRC failure'}]}:url.endsWith('/history')?[{valid:true,seconds:0,pressure_pa:0},{valid:false,seconds:1},{valid:true,seconds:2,pressure_pa:1}]:{queued:true},text:async()=>''}}});
 const source=fs.readFileSync(path.join(__dirname,'../web/console.js'),'utf8');vm.runInContext(source,context,{filename:path.join(__dirname,'../web/console.js')});
 return {context,get,tabs,actions,status,calls,clock:value=>now=value,fail:value=>fail=value,reject:value=>reject=value,run:code=>vm.runInContext(code,context)};
}
test('page is light, plain and has no login or chart assets',()=>{const html=fs.readFileSync(path.join(__dirname,'../web/index.html'),'utf8');assert.match(html,/color-scheme:light/);assert.doesNotMatch(html,/<svg|id="token"|id="chart"/);assert.match(html,/does not provide a separate pressure/);});
test('automatic public status shows actual differential values and their processing difference',async()=>{const u=setup();await u.run('refresh()');assert.equal(u.get('temperature').textContent,'80.6');assert.equal(u.get('sensor-pressure').textContent,'2.050');assert.equal(u.get('corrected-pressure').textContent,'3.000');assert.equal(u.get('pressure-difference').textContent,'0.950');assert.equal(u.get('state').textContent,'Live sensor data');assert.equal(u.calls[0].options.headers.Authorization,undefined);assert.equal(u.get('metrics-link').href,'http://unit-device/metrics');assert.match(u.get('json').textContent,/processing_difference_pa/);});
test('stale or invalid samples never look like live readings',async()=>{const u=setup();await u.run('refresh()');u.clock(9000);u.run('freshness()');assert.equal(u.get('sensor-pressure').textContent,'—');u.status.valid=false;await u.run('refresh()');assert.equal(u.get('state').textContent,'Sample invalid or stale');assert.equal(u.get('corrected-pressure').textContent,'—');});
test('connection failures hide values and background pages do not poll',async()=>{const u=setup();u.fail(true);await u.run('refresh()');assert.equal(u.get('state').textContent,'Connection error; retrying');assert.equal(u.get('temperature').textContent,'—');u.fail(false);await u.run('refresh()');assert.equal(u.get('message').textContent,'');assert.equal(u.get('state').textContent,'Live sensor data');const n=u.calls.length;u.run('document.hidden=true');await u.run('refresh()');assert.equal(u.calls.length,n);});
test('local controls use JSON with explicit action acknowledgment and no bearer',async()=>{const u=setup();await u.run('refresh()');await u.run('configure({power_save:true})');let c=u.calls.at(-1);assert.equal(c.options.headers.Authorization,undefined);assert.equal(c.options.headers['Content-Type'],'application/json');assert.equal(JSON.parse(c.options.body).persist,false);await u.run("action('sensor_reset')");assert.equal(JSON.parse(u.calls.at(-1).options.body).acknowledge_interruption,true);});
test('recent logs are requested only by the explicit button and stay text',async()=>{const u=setup();await u.run('refresh()');assert.equal(u.calls.length,1);await u.get('load-logs').onclick();assert.match(u.get('logs').textContent,/CRC failure/);assert.equal(u.calls.at(-1).url,'/api/v1/logs');});

test('summary reports Wi-Fi signal, freshness has a non-color label and manual refresh works',async()=>{const u=setup();u.status.network.bssid='02:00:00:00:00:01';u.status.network.rssi=-75;await u.get('refresh-now').onclick();assert.match(u.get('connection-summary').textContent,/Wi-Fi connected.*-75/);assert.equal(u.get('state').dataset.state,'live');u.status.valid=false;await u.run('refresh()');assert.equal(u.get('state').dataset.state,'stale');assert.equal(u.get('corrected-pressure').textContent,'—');});
test('syslog counters are diagnostic text and do not imply receipt',async()=>{const u=setup();u.status.syslog={destination:'192.0.2.2:514/UDP',sent:7,errors:1,dropped:2,delivery:'sent is not delivery confirmation'};await u.run('refresh()');const table=u.get('device-data').children.map(tr=>tr.children.map(td=>td.textContent).join(' ')).join('\n');assert.match(table,/192\.0\.2\.2:514/);assert.match(table,/7 \/ 1 \/ 2/);assert.match(table,/not delivery confirmation/);});
