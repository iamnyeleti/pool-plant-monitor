#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include "dashboard.h"
#include "network.h"
#include "vibration.h"
#include "schedule.h"
#include "water_quality.h"
#include "radar.h"
#include "module_flags.h"

static unsigned long lastOccupancyHeartbeat = 0;
static unsigned long lastOccPrint = 0;
static String occupancySenderIP = "";
static int occupancyEventCount = 0;
static bool occupancyEventActive = false;
static float occupancyChangePercent = 0.0;
static String occupancyLastEventTime = "";

static const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en-ZA">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Pool plant monitor</title>
<link rel="icon" href="data:,">
<style>
@font-face{font-family:"AH";font-weight:400;font-style:normal;font-display:swap;src:url(data:font/woff2;base64,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) format("woff2")}
@font-face{font-family:"AH";font-weight:700;font-style:normal;font-display:swap;src:url(data:font/woff2;base64,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) format("woff2")}
:root{
  --surface:#F3F5F4;--card:#fff;--ink:#12262D;--muted:#4F666F;--rule:#D4DCDE;
  --primary:#0A5C73;--primary-hover:#07475A;--radius:8px;
  --font:"AH",system-ui,-apple-system,"Segoe UI",Roboto,Arial,sans-serif;--gutter:24px;
  --ok:#17653A;--ok-bg:#E4F3E9;--caution:#8A5A00;--caution-bg:#FEF3D6;
  --attn:#A3201A;--attn-bg:#FBE4E1;--off:#4F666F;--off-bg:#E6EBEC;
}
*,*::before,*::after{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--surface);color:var(--ink);font:400 17px/1.5 var(--font)}
h1,h2,h3,p,dl,ul{margin:0;padding:0}
ul{list-style:none}
button,input{font-family:var(--font)}
:focus-visible{outline:3px solid var(--primary);outline-offset:3px}
[hidden]{display:none!important}
.shell{max-width:1200px;margin:0 auto;padding:0 var(--gutter)}

/* simulated banner */
.sim{background:#12262D;color:#fff;padding:10px var(--gutter);text-align:center;font-size:16px}
.sim strong{font-weight:700;letter-spacing:.02em}

/* header */
.hdr{display:flex;flex-wrap:wrap;justify-content:space-between;align-items:baseline;gap:6px 24px;padding:18px 0 12px}
h1{font-size:26px;line-height:1.3;font-weight:700}
.meta{display:flex;flex-wrap:wrap;gap:4px 20px;font-size:16px;color:var(--muted)}
.conn{font-weight:700}
.conn.ok{color:var(--ok)}.conn.bad{color:var(--attn)}
.dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:6px;vertical-align:baseline}
.dot.live{background:var(--ok)}.dot.hollow{border:2px solid currentColor;width:10px;height:10px}
.dot.warn{background:var(--attn)}.dot.grey{background:#8DA0A7}.dot.amber{background:#C58A00}

/* overall strip */
.overall{border-radius:var(--radius);padding:14px 20px;margin-bottom:20px;border-left:6px solid var(--off);background:var(--off-bg)}
.overall h2{font-size:21px;font-weight:700;line-height:1.3}
.overall ul{margin-top:6px;font-size:16px}
.overall li{padding:1px 0}
.overall li::before{content:"\2022\00a0";}
.overall[data-level="ok"]{background:var(--ok-bg);border-color:var(--ok)}
.overall[data-level="ok"] h2{color:var(--ok)}
.overall[data-level="caution"]{background:var(--caution-bg);border-color:#C58A00}
.overall[data-level="caution"] h2{color:var(--caution)}
.overall[data-level="attention"]{background:var(--attn-bg);border-color:var(--attn)}
.overall[data-level="attention"] h2{color:var(--attn)}

/* grid and cards */
.grid{display:grid;grid-template-columns:1fr 1fr;gap:20px;padding-bottom:28px;align-items:stretch}
.card{background:var(--card);border:1px solid var(--rule);border-left:5px solid var(--rule);border-radius:var(--radius);overflow:hidden;min-width:0}
.card[data-v="ok"]{border-left-color:var(--ok)}
.card[data-v="caution"]{border-left-color:#C58A00;background:#FFFCF3}
.card[data-v="attention"]{border-left-color:var(--attn);background:#FFF8F7}
.card[data-v="off"]{border-left-color:#8DA0A7}
.chead{display:flex;align-items:center;gap:12px;padding:14px 20px;border-bottom:1px solid var(--rule)}
.chead h2{font-size:19px;font-weight:700;flex:1;line-height:1.3}
.cbody{padding:8px 20px 20px}
.pill{display:inline-block;font-size:14px;font-weight:700;padding:4px 10px;border-radius:20px;white-space:nowrap}
.p-ok{background:var(--ok-bg);color:var(--ok)}.p-caution{background:var(--caution-bg);color:var(--caution)}
.p-attn{background:var(--attn-bg);color:var(--attn)}.p-off{background:var(--off-bg);color:var(--off)}
.is-lost .pill{display:none}
.is-lost .chead::after{content:"Last known";font-size:13px;font-weight:700;padding:4px 10px;border-radius:20px;background:var(--off-bg);color:var(--off)}
.is-lost .card{border-left-color:#8DA0A7;background:var(--card)}
.is-lost .dot.live{background:#8DA0A7}
.chead h2{min-width:0}

.msg{font-size:16px;color:var(--muted);margin:12px 0 4px}
.row{display:flex;justify-content:space-between;align-items:baseline;gap:12px;padding:11px 0;border-bottom:1px solid var(--rule)}
.row:last-child{border-bottom:0}
.rl{font-size:16px;color:var(--muted)}
.rv{font-size:19px;font-weight:700;text-align:right}
.rv small{font-size:15px;font-weight:400;color:var(--muted);margin-left:3px}
.rv .nd{font-size:16px;font-weight:400;color:var(--muted)}
.big{display:flex;justify-content:space-between;align-items:baseline;gap:12px;margin:12px 0 14px}
.big .rl{font-size:17px}.big b{font-size:36px;font-weight:700;line-height:1.1}
.t-ok{color:var(--ok)}.t-caution{color:var(--caution)}.t-attn{color:var(--attn)}.t-off{color:var(--off)}

/* scale */
.track{position:relative;height:14px;border-radius:7px;background:#D0DDD9;margin-top:6px}
.tick{position:absolute;top:-4px;width:2px;height:22px;margin-left:-1px;background:var(--ink)}
.marker{position:absolute;top:-7px;width:0;transition:left .5s ease}
.marker::before{content:"";position:absolute;left:-3px;top:0;width:6px;height:28px;border-radius:3px;background:var(--ink);box-shadow:0 0 0 2px var(--card)}
.legend{display:flex;flex-wrap:wrap;gap:4px 18px;margin-top:14px;font-size:14px;color:var(--muted)}
.legend span::before{content:"";display:inline-block;width:12px;height:12px;border-radius:2px;margin-right:6px;vertical-align:-1px;background:var(--c);box-shadow:inset 0 0 0 1px rgba(0,0,0,.25)}
.legend .lb::before{width:2px;height:15px;margin:0 7px 0 5px;border-radius:0;box-shadow:none}
.btn-off{margin-top:14px;padding:9px 14px;font-size:15px;font-weight:700;color:var(--muted);background:var(--off-bg);border:1px solid var(--rule);border-radius:var(--radius);cursor:not-allowed}
.note{font-size:14px;color:var(--muted);margin-top:10px}

/* water channels */
.ch{padding:13px 0;border-bottom:1px solid var(--rule)}
.ch:last-of-type{border-bottom:0}
.ch-top{display:flex;justify-content:space-between;align-items:baseline;gap:12px}
.ch-name{font-size:17px;font-weight:700}
.ch-val{font-size:24px;font-weight:700;text-align:right}
.ch-val small{font-size:15px;font-weight:400;color:var(--muted);margin-left:3px}
.ch-val .nd{font-size:17px;font-weight:400;color:var(--muted)}
.ch-st{font-size:16px;font-weight:700;margin-top:2px}
.ch-note{font-size:14px;color:var(--muted);margin-top:2px}
.tag{display:inline-block;font-size:13px;font-weight:700;padding:2px 7px;border-radius:4px;background:#12262D;color:#fff;margin-left:6px;vertical-align:middle}

/* preferences form */
.tgl{margin-top:12px;padding:8px 14px;font-size:15px;font-weight:700;color:var(--primary);background:none;border:1px solid var(--primary);border-radius:var(--radius);cursor:pointer;min-height:44px}
.tgl:hover{background:rgba(10,92,115,.06)}
.pref{margin-top:14px;padding:16px;background:var(--surface);border-radius:var(--radius)}
.pref h3{font-size:16px;margin-bottom:10px}
.fr{display:flex;flex-wrap:wrap;gap:10px;margin-bottom:10px}
.ff{flex:1;min-width:110px}
.ff label{display:block;font-size:14px;color:var(--muted);margin-bottom:4px}
.ff input{width:100%;height:44px;padding:0 10px;font-size:18px;color:var(--ink);background:#fff;border:1px solid #7D939B;border-radius:5px}
.btn{min-height:46px;padding:0 20px;font-size:17px;font-weight:700;color:#fff;background:var(--primary);border:0;border-radius:var(--radius);cursor:pointer}
.btn:hover{background:var(--primary-hover)}.btn:disabled{background:#8DA0A7;cursor:default}
.fm{min-height:22px;margin-top:8px;font-size:15px}
.fm.ok{color:var(--ok)}.fm.err{color:var(--attn)}

/* module list */
.mods li{display:flex;justify-content:space-between;align-items:center;padding:7px 0;border-bottom:1px solid var(--rule);font-size:16px}
.mods li:last-child{border-bottom:0}
.mods h3{font-size:16px;margin:14px 0 4px}

/* demo controls */
.demo{margin:8px 0 40px;border:2px dashed #7D939B;border-radius:var(--radius);padding:14px 20px}
.demo summary{font-weight:700;cursor:pointer;min-height:44px;display:flex;align-items:center}
.demo p{font-size:15px;color:var(--muted);margin-bottom:8px}
.demo .b{display:flex;flex-wrap:wrap;gap:6px}
.demo button{min-height:42px;padding:0 12px;font-size:15px;color:var(--ink);background:#fff;border:1px solid #7D939B;border-radius:5px;cursor:pointer}
.demo button[aria-pressed="true"]{background:#12262D;color:#fff}
.foot{font-size:14px;color:var(--muted);padding:0 0 36px}
@media(max-width:860px){.grid{grid-template-columns:1fr}.big b{font-size:32px}}
@media(prefers-reduced-motion:reduce){.marker{transition:none}}
</style>
<style id="white-desert-inspired">
:root{
  color-scheme:light;
  --surface:#f4f4f0;
  --card:#ffffff;
  --ink:#202c40;
  --muted:#5e6872;
  --rule:#d9dcdd;
  --primary:#236d78;
  --primary-hover:#174f59;
  --radius:2px;
  --gutter:clamp(18px,3.2vw,48px);
  --ok:#236d58;
  --ok-bg:#e9f2ec;
  --caution:#895b1e;
  --caution-bg:#f7efdf;
  --attn:#a74440;
  --attn-bg:#f8eae8;
  --off:#5e6872;
  --off-bg:#eef0ef;
  --font:"AH","Inter Tight","Helvetica Neue",Arial,sans-serif;
  --display:"Baskerville","Bodoni MT","Didot",Georgia,serif;
}
html{min-height:100%;background:var(--surface);color-scheme:light;scroll-behavior:smooth}
body{min-height:100vh;margin:0;background:var(--surface);color:var(--ink);font:400 17px/1.55 var(--font);letter-spacing:0;-webkit-font-smoothing:antialiased}
::selection{background:#dce9e7;color:#182839}
button,input{font:inherit}
:focus-visible{outline:2px solid var(--primary);outline-offset:3px}
.shell{max-width:1480px;margin:0 auto;padding:0 var(--gutter)}

/* A restrained, editorial header: typography and a single hairline do the work. */
.hdr{display:flex;flex-wrap:wrap;justify-content:space-between;align-items:flex-end;gap:14px 28px;padding:30px 0 22px;margin-bottom:24px;border-bottom:1px solid #cfd4d5}
.hdr::before{display:none}
.hdr h1{padding:0;color:var(--ink);font-family:var(--display);font-size:clamp(34px,3.4vw,48px);font-weight:400;line-height:1.03;letter-spacing:-.035em}
.meta{display:flex;flex-wrap:wrap;align-items:center;justify-content:flex-end;gap:8px 16px;color:var(--muted);font-size:14px;font-variant-numeric:tabular-nums}
#clock{padding:0;border:0;border-radius:0;background:transparent;white-space:nowrap}
.conn{display:inline-flex;align-items:center;gap:7px;padding:5px 0 5px 12px;border:0;border-left:1px solid var(--rule);border-radius:0;background:transparent;color:var(--ink);font-size:12px;font-weight:700;letter-spacing:.09em;text-transform:uppercase;white-space:nowrap}
.conn.ok{color:var(--ok);background:transparent;border-color:var(--rule)}
.conn.bad{color:var(--attn);background:transparent;border-color:var(--rule)}
.dot{display:inline-block;width:7px;height:7px;border-radius:50%;margin-right:5px;vertical-align:baseline;box-shadow:none}
.dot.live{background:var(--ok);box-shadow:none}.dot.hollow{width:8px;height:8px;border:1px solid currentColor;background:transparent;box-shadow:none}.dot.warn{background:var(--attn)}.dot.grey{background:#95a0a3}.dot.amber{background:#a9792a}
.tag{display:inline-block;margin-left:8px;padding:3px 6px;border:1px solid #d8c2a5;border-radius:0;background:#f8efe3;color:#7b4c20;font-size:10px;font-weight:700;letter-spacing:.11em;text-transform:uppercase;vertical-align:middle}

/* Simulation banner remains clear, but deliberately looks like an engineering notice. */
.sim{position:relative;padding:10px var(--gutter);border:0;border-bottom:1px solid #e0c4a5;border-radius:0;background:#f3e7d8;color:#66472c;text-align:left;font-size:13px;line-height:1.45;letter-spacing:0}
.sim strong{margin-right:10px;color:#77451c;font-size:11px;letter-spacing:.12em}

/* Summary state and data cards use straight edges, hairlines and generous space. */
.overall{position:relative;overflow:hidden;margin-bottom:24px;padding:20px 24px;border:1px solid var(--rule);border-left:3px solid var(--off);border-radius:0;background:#fff;box-shadow:none}
.overall::after{display:none}
.overall h2{position:relative;z-index:1;color:var(--ink);font-size:21px;font-weight:700;line-height:1.3;letter-spacing:-.02em}
.overall ul{position:relative;z-index:1;display:flex;flex-direction:column;gap:3px;margin-top:8px;color:var(--muted);font-size:15px;line-height:1.5}
.overall li{padding:0}
.overall li::before{content:"";display:inline-block;width:5px;height:5px;border-radius:50%;margin:0 10px 2px 1px;background:currentColor;opacity:.75}
.overall[data-level="ok"]{border-color:#d5e2da;border-left-color:var(--ok);background:#f7faf7}
.overall[data-level="ok"] h2{color:var(--ok)}
.overall[data-level="caution"]{border-color:#e7dbc3;border-left-color:#a9792a;background:#fcf9f2}
.overall[data-level="caution"] h2{color:var(--caution)}
.overall[data-level="attention"]{border-color:#e8d0cd;border-left-color:var(--attn);background:#fcf7f6}
.overall[data-level="attention"] h2{color:var(--attn)}
.overall[data-level="lost"]{border-left-color:#89969b}

.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:20px;padding-bottom:30px;align-items:stretch}
.card{position:relative;isolation:isolate;min-width:0;overflow:hidden;border:1px solid var(--rule);border-left:2px solid #b8c1c2;border-radius:0;background:#fff;box-shadow:none;transition:border-color .15s ease}
.card::before{display:none}
.card[data-v="ok"]{border-left-color:var(--ok);background:#fff}
.card[data-v="caution"]{border-left-color:#b18434;background:#fffdf9}
.card[data-v="attention"]{border-left-color:var(--attn);background:#fffafa}
.card[data-v="off"]{border-left-color:#aeb8ba;background:#fff}
.chead{display:flex;align-items:center;gap:14px;min-height:68px;padding:17px 23px;border-bottom:1px solid var(--rule);background:#fbfbf8}
.chead h2{flex:1;min-width:0;color:var(--ink);font-size:19px;font-weight:700;line-height:1.3;letter-spacing:-.018em}
.cbody{padding:12px 23px 24px}
.pill{display:inline-flex;align-items:center;justify-content:center;min-height:27px;padding:4px 8px;border:1px solid var(--rule);border-radius:0;background:#f3f4f1;color:var(--off);font-size:11px;line-height:1.25;font-weight:700;letter-spacing:.08em;text-transform:uppercase;white-space:nowrap;box-shadow:none}
.p-ok{border-color:#cfdfd5;background:var(--ok-bg);color:var(--ok)}
.p-caution{border-color:#e6d7b8;background:var(--caution-bg);color:var(--caution)}
.p-attn{border-color:#e5c7c4;background:var(--attn-bg);color:var(--attn)}
.p-off{border-color:#d9dedd;background:var(--off-bg);color:var(--off)}
.is-lost .pill{display:none}
.is-lost .chead::after{content:"Last known";padding:4px 8px;border:1px solid #d9dedd;border-radius:0;background:var(--off-bg);color:var(--off);font-size:10px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}
.is-lost .card{border-left-color:#aeb8ba;background:#fff}
.is-lost .dot.live{background:#95a0a3;box-shadow:none}

.msg{margin:13px 0 5px;color:var(--muted);font-size:15px;line-height:1.55}
.row{display:flex;justify-content:space-between;align-items:baseline;gap:14px;padding:13px 0;border-bottom:1px solid #e6e8e7}
.row:last-child{border-bottom:0}
.rl{color:var(--muted);font-size:15px;line-height:1.45}
.rv{color:var(--ink);font-size:20px;font-weight:700;line-height:1.35;text-align:right;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
.rv small{margin-left:4px;color:var(--muted);font-size:13px;font-weight:500}
.rv .nd{color:var(--muted);font-size:15px;font-weight:400}
.big{display:flex;justify-content:space-between;align-items:center;gap:14px;margin:17px 0 19px;padding:16px 18px;border:1px solid #dfe5e4;border-radius:0;background:#f5f8f6;box-shadow:none}
.big .rl{font-size:15px}
.big b{color:var(--ink);font-size:clamp(36px,3.2vw,45px);font-weight:500;line-height:1.02;letter-spacing:-.035em;font-variant-numeric:tabular-nums}
.t-ok{color:var(--ok)}.t-caution{color:var(--caution)}.t-attn{color:var(--attn)}.t-off{color:var(--off)}

.track{position:relative;height:10px;margin-top:9px;border-radius:0;background:#e3e7e4;box-shadow:none;overflow:visible}
.tick{position:absolute;top:-4px;width:2px;height:18px;margin-left:-1px;border-radius:0;background:var(--ink);box-shadow:none}
.marker{position:absolute;top:-5px;width:0;transition:left .4s ease}
.marker::before{content:"";position:absolute;left:-2px;top:0;width:4px;height:20px;border-radius:0;background:var(--ink);box-shadow:none}
.legend{display:flex;flex-wrap:wrap;gap:8px 16px;margin-top:14px;color:var(--muted);font-size:13px;line-height:1.45}
.legend span::before{content:"";display:inline-block;width:10px;height:10px;border-radius:0;margin-right:6px;vertical-align:-1px;background:var(--c);box-shadow:none}
.legend .lb::before{width:2px;height:14px;margin:0 7px 0 5px;border-radius:0;box-shadow:none}
.btn-off{margin-top:15px;padding:10px 13px;border:1px solid #d6dcda;border-radius:0;background:#f0f2ef;color:#66726f;font-size:14px;font-weight:700;cursor:not-allowed}
.note{margin-top:11px;color:var(--muted);font-size:13px;line-height:1.55}

.ch{padding:15px 0;border-bottom:1px solid #e4e7e5}
.ch:last-of-type{border-bottom:0}
.ch-top{display:flex;justify-content:space-between;align-items:baseline;gap:12px}
.ch-name{color:var(--ink);font-size:16px;font-weight:700}
.ch-val{color:var(--ink);font-size:clamp(25px,2.4vw,30px);font-weight:600;line-height:1.25;letter-spacing:-.025em;text-align:right;font-variant-numeric:tabular-nums}
.ch-val small{margin-left:4px;color:var(--muted);font-size:14px;font-weight:400;letter-spacing:0}
.ch-val .nd{color:var(--muted);font-size:15px;font-weight:400}
.ch-st{margin-top:4px;font-size:15px;font-weight:700}
.ch-note{margin-top:4px;color:var(--muted);font-size:13px;line-height:1.55}

.tgl,.eng-toggle{display:inline-flex;align-items:center;justify-content:center;gap:8px;min-height:44px;margin-top:14px;padding:10px 15px;border:1px solid #c8d6d7;border-radius:1px;background:#f7faf8;color:#205f68;font-size:14px;font-weight:700;cursor:pointer;box-shadow:none;transition:background .15s ease,border-color .15s ease}
.tgl:hover,.eng-toggle:hover{border-color:#7dabad;background:#eaf3f1}
.tgl:active,.btn:active,.eng-toggle:active{transform:none}
.pref{margin-top:15px;padding:18px;border:1px solid #d9dfdc;border-radius:0;background:#f8f8f4;box-shadow:none}
.pref h3{margin-bottom:12px;color:var(--ink);font-size:17px;font-weight:700}
.fr{display:flex;flex-wrap:wrap;gap:12px;margin-bottom:12px}
.ff{flex:1;min-width:110px}
.ff label{display:block;margin-bottom:5px;color:var(--muted);font-size:14px}
.ff input{width:100%;height:46px;padding:0 11px;border:1px solid #c9d1cf;border-radius:0;background:#fff;color:var(--ink);font-size:18px;box-shadow:none}
.ff input:hover{border-color:#7b9b9f}
.ff input:focus{outline:2px solid #a8cccd;border-color:#236d78}
.btn{min-height:45px;padding:0 19px;border:1px solid #1e5861;border-radius:1px;background:#245e68;color:#fff;font-size:15px;font-weight:700;cursor:pointer;box-shadow:none;transition:background .15s ease}
.btn:hover{background:#174d56;box-shadow:none}
.btn:disabled{border-color:#c1c9c7;background:#c1c9c7;color:#f9fbf9;cursor:default}
.fm{min-height:22px;margin-top:8px;font-size:14px}
.fm.ok{color:var(--ok)}.fm.err{color:var(--attn)}
.mods h3{margin:19px 0 7px;color:var(--muted);font-family:var(--font);font-size:12px;font-weight:700;letter-spacing:.12em;text-transform:uppercase}
.mods li{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:11px 0;border-bottom:1px solid #e6e8e7;color:var(--ink);font-size:15px}
.mods li:last-child{border-bottom:0}
.mods li>span:last-child{color:var(--muted);font-size:14px}

.demo{margin:4px 0 30px;padding:17px 20px;border:1px solid var(--rule);border-radius:0;background:#fbfbf8;box-shadow:none}
.demo summary{display:flex;align-items:center;min-height:44px;color:var(--ink);font-size:15px;font-weight:700;cursor:pointer}
.demo summary::after{border-color:var(--rule);border-radius:0;color:var(--primary)}
.demo p{margin-bottom:10px;color:var(--muted);font-size:14px}
.demo .b{display:flex;flex-wrap:wrap;gap:7px}
.demo button{min-height:40px;padding:0 13px;border:1px solid #d4dbd9;border-radius:0;background:#fff;color:#334655;font-size:13px;cursor:pointer}
.demo button:hover{border-color:#91afb1;background:#f2f7f4;color:#205f68}
.demo button[aria-pressed="true"]{border-color:#2c6670;background:#2c6670;color:#fff;box-shadow:none}
.foot{padding:0 0 30px;color:var(--muted);font-size:13px;line-height:1.6}

/* Monitoring history: flat technical plotting surface, no decorative card effects. */
.history-panel{margin:21px 0 2px;padding:15px 15px 12px;border:1px solid #d9e0de;border-radius:0;background:#fafbf8;box-shadow:none}
.history-head{display:flex;align-items:baseline;justify-content:space-between;gap:12px}
.history-head strong{color:var(--ink);font-size:17px;font-weight:700}
.history-head span{color:var(--muted);font-size:12px;font-variant-numeric:tabular-nums}
.history-sub{margin:3px 0 6px;color:var(--muted);font-size:13px}
.vib-chart{display:block;width:100%;height:auto;overflow:visible}
.history-hint{margin:3px 0 7px;color:var(--muted);font-size:13px;line-height:1.5}
.history-legend{display:flex;flex-wrap:wrap;gap:8px 16px;margin-top:4px;color:var(--muted);font-size:12px}
.history-legend span{display:inline-flex;align-items:center;gap:6px}
.history-legend i{display:inline-block;width:19px;height:2px;border-radius:0;background:#277b81}
.history-legend .line-base{height:0;border-top:1px dashed #899da0;background:transparent}
.history-legend .line-warn{height:0;border-top:1px dashed #b18434;background:transparent}
.engineering-block{margin-top:10px;padding-top:8px;border-top:1px solid #e0e5e2}
.eng-toggle{margin-top:8px}
.engineering-values{margin-top:10px;padding:10px 14px;border:1px solid #d8dfdc;border-radius:0;background:#f8f9f5}
.engineering-values[hidden]{display:none!important}
.engineering-values .row{padding:10px 0}
.engineering-values .rl{font-size:14px}
.engineering-values .rv{font-size:17px}
.engineering-note{margin:10px 0 2px;color:var(--muted);font-size:13px;line-height:1.55}
.capture-panel{margin:15px 0 10px;padding:14px;border:1px solid #e4d5bc;border-radius:0;background:#fbf7f0;box-shadow:none}
.capture-head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:10px}
.capture-head strong{color:#483d2d;font-size:17px;font-weight:700}
.capture-trigger{display:inline-flex;align-items:center;padding:4px 7px;border:1px solid #e4d1af;border-radius:0;background:#f2e7d5;color:#77531f;font-size:11px;font-weight:700;letter-spacing:.06em;text-transform:uppercase;white-space:nowrap}
.capture-empty{display:flex;align-items:flex-start;gap:12px;padding:13px;border:1px dashed #d5c4a5;border-radius:0;background:rgba(255,255,255,.65)}
.capture-symbol{display:none}
.capture-empty strong{display:block;margin-bottom:3px;color:#483d2d;font-size:16px}
.capture-empty p{margin:0;color:#6b5d47;font-size:14px;line-height:1.55}
.capture-image{display:block;width:100%;max-height:330px;object-fit:contain;border:1px solid #d9cbb3;border-radius:0;background:#f7f4ed}
.capture-caption{margin:8px 1px 0;color:#6b5d47;font-size:13px;line-height:1.5}

@media(min-width:1200px){.grid{gap:22px}.cbody{padding:14px 25px 25px}.chead{padding:18px 25px}}
@media(max-width:860px){.grid{grid-template-columns:1fr;gap:16px}.big b{font-size:38px}.hdr{align-items:flex-end;gap:13px 20px}.meta{justify-content:flex-start}}
@media(max-width:520px){
  :root{--gutter:16px}
  body{font-size:16px;line-height:1.52}
  .hdr{align-items:flex-start;gap:12px;padding:24px 0 17px;margin-bottom:18px}
  .hdr h1{max-width:100%;font-size:34px;line-height:1.04}
  .meta{width:100%;justify-content:flex-start;gap:8px 14px;font-size:13px}
  #clock{font-size:13px}
  .conn{font-size:11px;padding:5px 0 5px 10px}
  .overall{padding:16px 17px;margin-bottom:17px}
  .overall h2{font-size:19px}
  .overall ul{font-size:14px}
  .grid{gap:14px;padding-bottom:24px}
  .chead{min-height:60px;padding:14px 16px;gap:10px}
  .chead h2{font-size:18px}
  .cbody{padding:8px 16px 18px}
  .pill{font-size:10px;min-height:25px;padding:4px 6px;letter-spacing:.04em}
  .msg{font-size:14px}
  .big{padding:13px 14px;margin:14px 0 16px}
  .big .rl{font-size:14px}
  .big b{font-size:34px}
  .row{gap:10px;padding:11px 0}
  .rl{font-size:14px}
  .rv{font-size:18px}
  .ch-name{font-size:15px}
  .ch-val{font-size:25px}
  .pref{padding:14px}
  .btn{width:100%}
  .demo{padding:14px}
  .history-panel{padding:12px 10px 10px}
  .history-head strong{font-size:16px}
  .history-head span,.history-sub,.history-legend{font-size:11px}
  .capture-head{align-items:flex-start;flex-direction:column;gap:7px}
}
@media(prefers-reduced-motion:reduce){html{scroll-behavior:auto}*,*::before,*::after{transition:none!important;animation:none!important}}
</style>

<style id="crystal-water-operational">
:root{
  color-scheme:light;
  --surface:#eaf7f8;
  --card:rgba(255,255,255,.95);
  --ink:#123247;
  --muted:#456577;
  --rule:rgba(24,84,108,.22);
  --primary:#087d9c;
  --primary-hover:#056782;
  --radius:3px;
  --gutter:clamp(18px,3.2vw,48px);
  --ok:#086b4c;
  --ok-bg:#d9f8e8;
  --caution:#784b00;
  --caution-bg:#fff0c6;
  --attn:#9e2537;
  --attn-bg:#ffe3e7;
  --off:#3c5868;
  --off-bg:#e3eff3;
  --font:"Inter","Helvetica Neue",Arial,sans-serif;
  --display:"Baskerville","Bodoni MT","Didot",Georgia,serif;
  --water-photo:url("data:image/webp;base64,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");
}
html{min-height:100%;background:#e7f8fa;color-scheme:light;scroll-behavior:smooth}
body{
  min-height:100vh;margin:0;color:var(--ink);
  font:400 18px/1.55 var(--font);letter-spacing:0;-webkit-font-smoothing:antialiased;
  background-color:#e5f6f8;
  background-image:linear-gradient(180deg,rgba(223,248,250,.60) 0%,rgba(241,250,250,.82) 42%,rgba(248,251,249,.95) 100%),var(--water-photo);
  background-size:100% 100%,cover;background-position:center top;background-repeat:no-repeat;background-attachment:fixed,fixed;
}
button,input{font:inherit}
::selection{background:#bce8ef;color:#092d42}
:focus-visible{outline:3px solid #148bb0;outline-offset:3px}
.shell{max-width:1560px;margin:0 auto;padding:0 var(--gutter)}

/* A photographic water surface gives the dashboard a real place and material. */
.hdr{
  position:relative;isolation:isolate;display:flex;flex-wrap:wrap;justify-content:space-between;align-items:flex-end;gap:22px 32px;
  width:100vw;left:50%;margin:0 0 28px -50vw;
  min-height:310px;padding:42px max(var(--gutter),calc((100vw - 1560px)/2 + var(--gutter))) 33px;
  overflow:hidden;border:0;border-bottom:1px solid rgba(19,67,87,.24);border-radius:0;
  background-color:#a7e7ee;
  background-image:linear-gradient(90deg,rgba(238,251,251,.95) 0%,rgba(237,250,250,.78) 37%,rgba(224,247,249,.22) 73%,rgba(7,70,100,.08) 100%),var(--water-photo);
  background-size:cover;background-position:center 53%;box-shadow:none;
}
.hdr::before{content:"";position:absolute;z-index:-1;inset:0;background:linear-gradient(180deg,rgba(255,255,255,.14),transparent 34%,rgba(8,65,89,.08));pointer-events:none}
.hdr h1{position:relative;max-width:900px;flex:1 1 570px;padding:0;color:#102f44;font-family:var(--display);font-size:clamp(50px,6.5vw,96px);font-weight:400;line-height:.92;letter-spacing:-.045em;text-transform:none;text-wrap:balance;text-shadow:0 1px 0 rgba(255,255,255,.3)}
.hdr .tag{margin:0 0 0 10px;vertical-align:middle;font-family:var(--font);font-size:11px;letter-spacing:.1em}
.meta{position:relative;display:flex;flex:0 1 340px;flex-wrap:wrap;justify-content:flex-end;align-items:center;gap:9px 12px;padding:10px 12px;border:1px solid rgba(255,255,255,.72);border-radius:3px;background:rgba(250,254,254,.7);color:#193d50;font-size:15px;font-weight:600;font-variant-numeric:tabular-nums;box-shadow:0 4px 24px rgba(14,73,95,.08);backdrop-filter:blur(12px);-webkit-backdrop-filter:blur(12px)}
#clock{padding:0;border:0;border-radius:0;background:transparent;white-space:nowrap;font-size:15px}
.conn{display:inline-flex;align-items:center;gap:8px;min-height:42px;padding:8px 12px;border:1px solid rgba(34,94,113,.23);border-radius:3px;background:rgba(255,255,255,.86);color:#284b5c;font-size:14px;font-weight:800;letter-spacing:.055em;text-transform:uppercase;white-space:nowrap;box-shadow:inset 0 1px 0 rgba(255,255,255,.9)}
.conn.ok{border-color:#82cfae;background:#e0f8e9;color:#075f43}
.conn.bad{border-color:#e7a3ac;background:#ffeaec;color:#932336}
.dot{display:inline-block;flex:0 0 auto;width:10px;height:10px;border-radius:50%;margin:0;background:currentColor;vertical-align:baseline;box-shadow:none}
.dot.live{background:#087e5c;box-shadow:0 0 0 3px rgba(8,126,92,.12)}
.dot.hollow{width:10px;height:10px;border:2px solid currentColor;background:transparent;box-shadow:none}
.dot.warn{background:#a56700}.dot.grey{background:#68818e}.dot.amber{background:#a56700}
.tag{display:inline-block;margin-left:8px;padding:5px 8px;border:1px solid #e1a25a;border-radius:2px;background:#fff3dc;color:#85460b;font-size:11px;font-weight:800;letter-spacing:.1em;text-transform:uppercase;vertical-align:middle}

.sim{position:relative;padding:12px var(--gutter);border:0;border-bottom:1px solid #e2b36c;border-radius:0;background:#fff0d0;color:#75450c;text-align:left;font-size:16px;line-height:1.45;letter-spacing:0}
.sim strong{margin-right:10px;color:#873e0a;font-size:12px;letter-spacing:.13em}

.overall{position:relative;overflow:hidden;margin:0 0 27px;padding:24px 28px;border:1px solid rgba(24,84,108,.2);border-left:8px solid #78929b;border-radius:3px;background:rgba(255,255,255,.94);box-shadow:0 7px 28px rgba(17,76,97,.07),inset 0 1px 0 rgba(255,255,255,.98);backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px)}
.overall::before{content:"SYSTEM STATUS";display:block;margin-bottom:5px;color:#486a7b;font-size:12px;font-weight:800;letter-spacing:.16em}
.overall::after{display:none}
.overall h2{position:relative;z-index:1;color:var(--ink);font-size:clamp(27px,2.6vw,36px);font-weight:700;line-height:1.15;letter-spacing:-.035em}
.overall ul{position:relative;z-index:1;display:flex;flex-direction:column;gap:5px;margin-top:12px;color:#38596b;font-size:17px;line-height:1.5}
.overall li{padding:0}
.overall li::before{content:"";display:inline-block;width:7px;height:7px;border-radius:50%;margin:0 11px 3px 1px;background:currentColor;opacity:.8}
.overall[data-level="ok"]{border-color:rgba(8,107,76,.24);border-left-color:#0b805c;background:linear-gradient(110deg,rgba(231,251,239,.97),rgba(255,255,255,.95) 58%)}
.overall[data-level="ok"] h2{color:#086444}
.overall[data-level="caution"]{border-color:rgba(166,103,0,.27);border-left-color:#bd7e11;background:linear-gradient(110deg,rgba(255,246,219,.98),rgba(255,255,255,.96) 58%)}
.overall[data-level="caution"] h2{color:#805000}
.overall[data-level="attention"]{border-color:rgba(158,37,55,.27);border-left-color:#bc3048;background:linear-gradient(110deg,rgba(255,233,235,.98),rgba(255,255,255,.96) 58%)}
.overall[data-level="attention"] h2{color:#9e2537}
.overall[data-level="lost"]{border-color:rgba(60,88,104,.22);border-left-color:#708c9a}

.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:23px;padding-bottom:32px;align-items:stretch}
.card{position:relative;isolation:isolate;min-width:0;overflow:hidden;border:1px solid rgba(21,80,101,.22);border-left:5px solid #90abb5;border-radius:3px;background:rgba(255,255,255,.95);box-shadow:0 8px 30px rgba(15,73,97,.085),inset 0 1px 0 rgba(255,255,255,.98);transition:border-color .15s ease,box-shadow .15s ease;backdrop-filter:blur(9px);-webkit-backdrop-filter:blur(9px)}
.card::before{display:none}
.card[data-v="ok"]{border-left-color:#11835f;background:rgba(255,255,255,.96)}
.card[data-v="caution"]{border-left-color:#bd7e11;background:rgba(255,253,247,.96)}
.card[data-v="attention"]{border-left-color:#bd3047;background:rgba(255,250,250,.97)}
.card[data-v="off"]{border-left-color:#9db2bb;background:rgba(255,255,255,.95)}
.chead{display:flex;align-items:center;gap:14px;min-height:78px;padding:20px 24px;border-bottom:1px solid rgba(24,84,108,.15);background:linear-gradient(180deg,rgba(237,249,250,.94),rgba(248,253,252,.88));}
.chead h2{flex:1;min-width:0;color:#12374a;font-size:clamp(21px,1.8vw,25px);font-weight:700;line-height:1.25;letter-spacing:-.025em}
.cbody{padding:16px 24px 27px}
.pill{display:inline-flex;flex:0 0 auto;align-items:center;justify-content:center;min-height:42px;padding:8px 12px;border:1px solid #c0d2d9;border-radius:3px;background:#e8f1f4;color:#315465;font-size:14px;line-height:1.2;font-weight:850;letter-spacing:.045em;text-transform:uppercase;white-space:nowrap;box-shadow:inset 0 1px 0 rgba(255,255,255,.9)}
.p-ok{border-color:#78caa5;background:#d9f8e8;color:#075d41}
.p-caution{border-color:#e4bd6a;background:#fff0c6;color:#794b00}
.p-attn{border-color:#e4a0aa;background:#ffe3e7;color:#98263a}
.p-off{border-color:#b9cbd2;background:#e3eff3;color:#345566}
.is-lost .pill{display:none}
.is-lost .chead::after{content:"Last known";display:inline-flex;align-items:center;justify-content:center;min-height:42px;padding:8px 12px;border:1px solid #b9cbd2;border-radius:3px;background:#e3eff3;color:#345566;font-size:14px;font-weight:850;letter-spacing:.045em;text-transform:uppercase}
.is-lost .card{border-left-color:#9db2bb;background:rgba(255,255,255,.95)}
.is-lost .dot.live{background:#68818e;box-shadow:none}
.msg{margin:15px 0 7px;color:#486779;font-size:16px;line-height:1.55}
.row{display:flex;justify-content:space-between;align-items:baseline;gap:14px;padding:15px 0;border-bottom:1px solid rgba(24,84,108,.14)}
.row:last-child{border-bottom:0}
.rl{color:#456577;font-size:16px;line-height:1.45}
.rv{color:#102f44;font-size:clamp(23px,2.1vw,29px);font-weight:750;line-height:1.28;text-align:right;font-variant-numeric:tabular-nums;overflow-wrap:anywhere;letter-spacing:-.025em}
.rv small{margin-left:5px;color:#496a7a;font-size:15px;font-weight:600;letter-spacing:0}
.rv .nd{color:#466575;font-size:17px;font-weight:500}
.big{display:flex;justify-content:space-between;align-items:center;gap:18px;margin:19px 0 22px;padding:20px 22px;border:1px solid rgba(7,125,156,.27);border-radius:3px;background:linear-gradient(135deg,rgba(227,249,251,.98),rgba(249,254,254,.93) 72%);box-shadow:inset 0 1px 0 rgba(255,255,255,.97),0 3px 14px rgba(14,102,127,.055)}
.big .rl{font-size:17px;font-weight:650;color:#31596b}
.big b{color:#0c4059;font-size:clamp(48px,5vw,68px);font-weight:600;line-height:.98;letter-spacing:-.05em;font-variant-numeric:tabular-nums}
.t-ok{color:var(--ok)}.t-caution{color:var(--caution)}.t-attn{color:var(--attn)}.t-off{color:var(--off)}
.track{position:relative;height:13px;margin-top:11px;border:1px solid rgba(8,92,122,.2);border-radius:2px;background:linear-gradient(180deg,#d9f4f7,#bce6ed);box-shadow:inset 0 1px 2px rgba(13,76,97,.12);overflow:visible}
.tick{position:absolute;top:-5px;width:2px;height:22px;margin-left:-1px;border-radius:1px;background:#163e52;box-shadow:0 0 0 2px rgba(255,255,255,.6)}
.marker{position:absolute;top:-6px;width:0;transition:left .4s ease}
.marker::before{content:"";position:absolute;left:-3px;top:0;width:6px;height:24px;border-radius:2px;background:#063e58;box-shadow:0 0 0 2px rgba(255,255,255,.8),0 2px 4px rgba(5,51,74,.18)}
.legend{display:flex;flex-wrap:wrap;gap:8px 18px;margin-top:15px;color:#456577;font-size:14px;line-height:1.45}
.legend span::before{content:"";display:inline-block;width:11px;height:11px;border-radius:2px;margin-right:7px;vertical-align:-1px;background:var(--c);box-shadow:inset 0 0 0 1px rgba(10,51,70,.15)}
.legend .lb::before{width:2px;height:15px;margin:0 8px 0 5px;border-radius:0;box-shadow:none}
.btn-off{margin-top:15px;padding:11px 15px;border:1px solid #c6d6dc;border-radius:3px;background:#edf4f5;color:#4b6a79;font-size:15px;font-weight:750;cursor:not-allowed}
.note{margin-top:12px;color:#496b7d;font-size:14px;line-height:1.55}
.ch{padding:17px 0;border-bottom:1px solid rgba(24,84,108,.14)}
.ch:last-of-type{border-bottom:0}
.ch-top{display:flex;justify-content:space-between;align-items:baseline;gap:12px}
.ch-name{color:#153a4e;font-size:18px;font-weight:750}
.ch-val{color:#0e3f57;font-size:clamp(31px,2.9vw,38px);font-weight:700;line-height:1.18;letter-spacing:-.035em;text-align:right;font-variant-numeric:tabular-nums}
.ch-val small{margin-left:5px;color:#496c7c;font-size:16px;font-weight:500;letter-spacing:0}
.ch-val .nd{color:#4d6b7a;font-size:17px;font-weight:500}
.ch-st{margin-top:5px;font-size:17px;font-weight:800}
.ch-note{margin-top:5px;color:#4c6c7d;font-size:14px;line-height:1.55}
.tgl,.eng-toggle{display:inline-flex;align-items:center;justify-content:center;gap:8px;min-height:48px;margin-top:16px;padding:11px 16px;border:1px solid #9fc7d0;border-radius:3px;background:linear-gradient(180deg,#fbffff,#e9f7f9);color:#075c75;font-size:16px;font-weight:800;cursor:pointer;box-shadow:inset 0 1px 0 rgba(255,255,255,.95);transition:background .15s ease,border-color .15s ease}
.tgl:hover,.eng-toggle:hover{border-color:#4b9eb0;background:#e0f4f7}
.pref{margin-top:16px;padding:20px;border:1px solid rgba(24,84,108,.2);border-radius:3px;background:rgba(246,252,251,.95);box-shadow:inset 0 1px 0 rgba(255,255,255,.98)}
.pref h3{margin-bottom:14px;color:#12374a;font-size:20px;font-weight:750}
.fr{display:flex;flex-wrap:wrap;gap:12px;margin-bottom:13px}
.ff{flex:1;min-width:110px}
.ff label{display:block;margin-bottom:6px;color:#456779;font-size:15px;font-weight:600}
.ff input{width:100%;height:50px;padding:0 12px;border:1px solid #adc4cc;border-radius:3px;background:#fff;color:#12374a;font-size:20px;box-shadow:inset 0 1px 3px rgba(15,76,97,.04)}
.ff input:hover{border-color:#5c9daf}
.ff input:focus{outline:3px solid #b8e9ef;border-color:#087d9c}
.btn{min-height:50px;padding:0 22px;border:1px solid #05627b;border-radius:3px;background:linear-gradient(180deg,#128eaa,#08728f);color:#fff;font-size:16px;font-weight:800;cursor:pointer;box-shadow:inset 0 1px 0 rgba(255,255,255,.24),0 2px 6px rgba(8,96,122,.13)}
.btn:hover{background:linear-gradient(180deg,#087d9c,#055d76);box-shadow:inset 0 1px 0 rgba(255,255,255,.22)}
.btn:disabled{border-color:#bccbd0;background:#bccbd0;color:#f9fbfb;cursor:default}
.fm{min-height:22px;margin-top:9px;font-size:15px}
.fm.ok{color:var(--ok)}.fm.err{color:var(--attn)}
.mods h3{margin:21px 0 8px;color:#486b7a;font-family:var(--font);font-size:13px;font-weight:850;letter-spacing:.13em;text-transform:uppercase}
.mods li{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:13px 0;border-bottom:1px solid rgba(24,84,108,.14);color:#12374a;font-size:17px}
.mods li:last-child{border-bottom:0}
.mods li>span:last-child{color:#456779;font-size:16px}
.demo{margin:4px 0 30px;padding:19px 22px;border:1px solid rgba(24,84,108,.24);border-radius:3px;background:rgba(249,253,252,.94);box-shadow:0 5px 18px rgba(15,73,97,.04)}
.demo summary{display:flex;align-items:center;min-height:48px;color:#12374a;font-size:17px;font-weight:800;cursor:pointer}
.demo summary::after{border-color:#b4cbd1;border-radius:2px;color:var(--primary)}
.demo p{margin-bottom:11px;color:#496b7b;font-size:15px}
.demo .b{display:flex;flex-wrap:wrap;gap:8px}
.demo button{min-height:45px;padding:0 15px;border:1px solid #c5d9de;border-radius:3px;background:#fff;color:#274c5e;font-size:15px;cursor:pointer;box-shadow:inset 0 1px 0 rgba(255,255,255,.95)}
.demo button:hover{border-color:#6eacbb;background:#eef9fa;color:#075d75}
.demo button[aria-pressed="true"]{border-color:#087d9c;background:#087d9c;color:#fff;box-shadow:inset 0 1px 0 rgba(255,255,255,.17)}
.foot{padding:0 0 34px;color:#4c6a79;font-size:14px;line-height:1.6}

/* Vibration trend: high-contrast plotting and a clean instrument-paper surface. */
.history-panel{margin:23px 0 2px;padding:17px 17px 13px;border:1px solid rgba(24,84,108,.2);border-radius:3px;background:linear-gradient(145deg,rgba(249,254,254,.98),rgba(234,248,249,.94));box-shadow:inset 0 1px 0 rgba(255,255,255,.98),0 3px 12px rgba(15,73,97,.045)}
.history-head{display:flex;align-items:baseline;justify-content:space-between;gap:12px}
.history-head strong{color:#12374a;font-size:20px;font-weight:800}
.history-head span{color:#486979;font-size:14px;font-variant-numeric:tabular-nums}
.history-sub{margin:4px 0 8px;color:#486979;font-size:15px}
.vib-chart{display:block;width:100%;height:auto;overflow:visible}
.history-hint{margin:4px 0 8px;color:#456779;font-size:14px;line-height:1.5}
.history-legend{display:flex;flex-wrap:wrap;gap:9px 18px;margin-top:6px;color:#456779;font-size:13px}
.history-legend span{display:inline-flex;align-items:center;gap:7px}
.history-legend i{display:inline-block;width:21px;height:3px;border-radius:1px;background:#087d9c}
.history-legend .line-base{height:0;border-top:2px dashed #8aa5ae;background:transparent}
.history-legend .line-warn{height:0;border-top:2px dashed #bd7e11;background:transparent}
.engineering-block{margin-top:12px;padding-top:10px;border-top:1px solid rgba(24,84,108,.18)}
.eng-toggle{margin-top:10px}
.engineering-values{margin-top:12px;padding:14px 16px;border:1px solid rgba(24,84,108,.2);border-radius:3px;background:rgba(246,252,251,.98)}
.engineering-values[hidden]{display:none!important}
.engineering-values .row{padding:12px 0}
.engineering-values .rl{font-size:15px}
.engineering-values .rv{font-size:20px}
.engineering-note{margin:11px 0 2px;color:#486979;font-size:14px;line-height:1.55}
.capture-panel{margin:17px 0 11px;padding:17px;border:1px solid #e1bd82;border-radius:3px;background:linear-gradient(140deg,rgba(255,247,227,.98),rgba(255,253,248,.96));box-shadow:inset 0 1px 0 rgba(255,255,255,.95)}
.capture-head{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:11px}
.capture-head strong{color:#65440f;font-size:20px;font-weight:800}
.capture-trigger{display:inline-flex;align-items:center;min-height:34px;padding:6px 10px;border:1px solid #ddac58;border-radius:3px;background:#ffebc0;color:#764907;font-size:12px;font-weight:850;letter-spacing:.08em;text-transform:uppercase;white-space:nowrap}
.capture-empty{display:flex;align-items:flex-start;gap:12px;padding:15px;border:1px dashed #d6b779;border-radius:3px;background:rgba(255,255,255,.78)}
.capture-symbol{display:none}
.capture-empty strong{display:block;margin-bottom:4px;color:#65440f;font-size:17px}
.capture-empty p{margin:0;color:#705b35;font-size:15px;line-height:1.55}
.capture-image{display:block;width:100%;max-height:360px;object-fit:contain;border:1px solid #d6bd94;border-radius:3px;background:#f7f8f5}
.capture-caption{margin:9px 1px 0;color:#705b35;font-size:14px;line-height:1.5}

@media(min-width:1200px){.grid{gap:25px}.cbody{padding:19px 27px 29px}.chead{padding:21px 27px}}
@media(max-width:860px){
  .grid{grid-template-columns:1fr;gap:18px}
  .hdr{min-height:275px;gap:18px 22px;padding-top:32px;padding-bottom:28px;align-items:flex-end}
  .hdr h1{flex:1 1 100%;max-width:800px;font-size:clamp(49px,9vw,76px)}
  .meta{flex:0 1 auto;justify-content:flex-start}
  .overall{margin-bottom:20px}
  .big b{font-size:54px}
}
@media(max-width:520px){
  :root{--gutter:16px}
  body{font-size:17px;line-height:1.5;background-attachment:scroll,scroll}
  .hdr{min-height:255px;gap:18px;padding-top:26px;padding-bottom:22px;background-position:58% center}
  .hdr h1{font-size:clamp(43px,12vw,60px);line-height:.94;max-width:100%;flex-basis:100%}
  .meta{width:auto;max-width:100%;gap:7px;padding:8px 9px}
  #clock{font-size:14px}
  .conn{min-height:40px;padding:7px 9px;font-size:12px}
  .sim{font-size:14px;padding-top:11px;padding-bottom:11px}
  .overall{padding:19px 18px;margin-bottom:17px;border-left-width:6px}
  .overall::before{font-size:11px}
  .overall h2{font-size:27px;line-height:1.2}
  .overall ul{font-size:15px;gap:5px}
  .grid{gap:16px;padding-bottom:24px}
  .chead{min-height:68px;padding:16px 15px;gap:10px;flex-wrap:wrap}
  .chead h2{flex:1 1 48%;font-size:21px}
  .pill,.is-lost .chead::after{min-height:40px;padding:8px 10px;font-size:13px;letter-spacing:.035em}
  .cbody{padding:9px 15px 20px}
  .msg{font-size:15px}
  .big{padding:16px 15px;margin:16px 0 18px;gap:12px}
  .big .rl{font-size:15px}
  .big b{font-size:clamp(43px,12vw,55px)}
  .row{gap:9px;padding:12px 0}
  .rl{font-size:15px}
  .rv{font-size:22px}
  .rv small{font-size:14px}
  .ch{padding:15px 0}
  .ch-name{font-size:16px}
  .ch-val{font-size:30px}
  .ch-val small{font-size:14px}
  .ch-st{font-size:16px}
  .pref{padding:15px}
  .pref h3{font-size:18px}
  .btn{width:100%}
  .demo{padding:14px}
  .history-panel{padding:13px 10px 11px;margin-top:18px}
  .history-head strong{font-size:18px}
  .history-head span,.history-sub{font-size:12px}
  .history-legend{font-size:12px}
  .capture-head{align-items:flex-start;flex-direction:column;gap:8px}
  .capture-head strong{font-size:18px}
  .capture-trigger{font-size:11px}
  .capture-empty{padding:12px}
  .foot{font-size:13px}
}
@media(prefers-reduced-motion:reduce){html{scroll-behavior:auto}*,*::before,*::after{transition:none!important;animation:none!important}}
</style>

<style id="glossy-water-finish">
:root{
  --gloss-line:rgba(255,255,255,.78);
  --gloss-shadow:0 16px 38px rgba(12,76,99,.12);
  --vib-accent:#118e9e;
  --vib-wash:rgba(230,250,251,.78);
}
body{transition:background-image .3s ease,background-color .3s ease}
/* Replace hard rules with a soft, light-fading seam. */
.hdr{border-bottom:0;background-image:linear-gradient(100deg,rgba(239,252,252,.91) 0%,rgba(239,252,252,.67) 39%,rgba(229,248,250,.18) 77%,rgba(7,70,100,.04) 100%),var(--water-photo)}
.hdr::before{background:linear-gradient(180deg,rgba(255,255,255,.34) 0%,rgba(255,255,255,.08) 34%,rgba(8,65,89,.04) 73%,rgba(8,65,89,.13) 100%)}
.hdr::after{content:"";position:absolute;z-index:2;left:0;right:0;bottom:0;height:3px;pointer-events:none;background:linear-gradient(90deg,transparent 0%,rgba(40,119,142,.12) 9%,rgba(255,255,255,.82) 34%,rgba(255,255,255,.9) 52%,rgba(40,119,142,.22) 85%,transparent 100%);filter:blur(.45px)}
.hdr h1{text-shadow:0 1px 0 rgba(255,255,255,.62),0 8px 28px rgba(15,73,97,.05)}
.meta{gap:12px 14px;padding:11px 12px 11px 16px;border:1px solid rgba(255,255,255,.62);border-radius:12px;background:linear-gradient(138deg,rgba(255,255,255,.81),rgba(242,252,253,.47));box-shadow:inset 0 1px 0 rgba(255,255,255,.98),inset 0 -1px 0 rgba(28,105,129,.05),0 13px 32px rgba(16,73,94,.11);backdrop-filter:blur(18px) saturate(1.18);-webkit-backdrop-filter:blur(18px) saturate(1.18)}
#clock{display:inline-flex;flex-direction:column;align-items:flex-start;justify-content:center;gap:1px;min-width:144px;padding:1px 10px 1px 0;color:#11384d;font-size:19px;font-weight:850;line-height:1.2;letter-spacing:.015em;font-variant-numeric:tabular-nums}
#clock::before{content:"PLANT TIME";display:block;color:#587887;font-family:var(--font);font-size:9px;font-weight:850;line-height:1.2;letter-spacing:.16em}
.conn{gap:8px;min-height:38px;padding:8px 13px;border:1px solid rgba(255,255,255,.72);border-radius:999px;background:linear-gradient(145deg,rgba(255,255,255,.78),rgba(245,252,251,.4));color:#285568;font-size:12px;font-weight:850;letter-spacing:.075em;box-shadow:inset 0 1px 0 rgba(255,255,255,.98),0 4px 15px rgba(17,76,97,.07);backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px);transition:background .25s ease,color .25s ease,box-shadow .25s ease,border-color .25s ease}
.conn.ok{border-color:rgba(75,190,143,.3);background:linear-gradient(140deg,rgba(229,255,243,.82),rgba(197,245,225,.56));color:#076447;box-shadow:inset 0 1px 0 rgba(255,255,255,.97),0 4px 16px rgba(26,139,101,.09)}
.conn.bad{border-color:rgba(219,106,126,.25);background:linear-gradient(140deg,rgba(255,242,244,.86),rgba(255,216,224,.58));color:#9d2946;box-shadow:inset 0 1px 0 rgba(255,255,255,.95),0 5px 19px rgba(190,52,83,.12)}
.conn.bad .dot{box-shadow:0 0 0 4px rgba(201,54,86,.10)}
.tag{border:1px solid rgba(224,161,83,.42);border-radius:999px;background:linear-gradient(145deg,rgba(255,249,236,.88),rgba(255,230,188,.56));box-shadow:inset 0 1px 0 rgba(255,255,255,.85)}
/* A notice that gently fades into the water instead of reading as a separate box. */
.sim{position:relative;margin:0 0 14px;padding:13px var(--gutter);border:0;border-radius:0;background:linear-gradient(90deg,rgba(255,239,206,.91) 0%,rgba(255,246,226,.68) 48%,rgba(255,249,238,.24) 79%,rgba(255,255,255,0) 100%);color:#76501c;box-shadow:inset 0 1px 0 rgba(255,255,255,.65);backdrop-filter:blur(11px);-webkit-backdrop-filter:blur(11px)}
.sim::after{content:"";position:absolute;left:var(--gutter);right:var(--gutter);bottom:0;height:1px;background:linear-gradient(90deg,rgba(211,157,72,.26),rgba(211,157,72,.12) 56%,transparent);pointer-events:none}
.sim strong{color:#855014;font-size:11px;letter-spacing:.15em}
/* The system state remains the clearest element, but its surface has a glass finish. */
.overall{border:1px solid rgba(255,255,255,.82);border-left:5px solid #7e9da8;border-radius:10px;background:linear-gradient(135deg,rgba(255,255,255,.84),rgba(246,253,252,.65));box-shadow:var(--gloss-shadow),inset 0 1px 0 rgba(255,255,255,.98);backdrop-filter:blur(17px) saturate(1.1);-webkit-backdrop-filter:blur(17px) saturate(1.1);transition:background .3s ease,border-color .3s ease,box-shadow .3s ease}
.overall::after{display:block;content:"";position:absolute;z-index:0;top:-42%;right:-6%;width:39%;height:160%;background:radial-gradient(ellipse at center,rgba(255,255,255,.56),rgba(255,255,255,0) 68%);pointer-events:none}
.overall[data-level="ok"]{border-color:rgba(255,255,255,.84);border-left-color:#168566;background:linear-gradient(112deg,rgba(225,250,238,.84),rgba(255,255,255,.66) 70%);box-shadow:0 14px 34px rgba(14,112,79,.08),inset 0 1px 0 rgba(255,255,255,.96)}
.overall[data-level="caution"]{border-color:rgba(255,255,255,.8);border-left-color:#b77d1c;background:linear-gradient(112deg,rgba(255,242,210,.91),rgba(255,255,255,.7) 72%);box-shadow:0 14px 34px rgba(161,109,19,.09),inset 0 1px 0 rgba(255,255,255,.97)}
.overall[data-level="attention"]{border-color:rgba(255,255,255,.8);border-left-color:#ca4c70;background:linear-gradient(112deg,rgba(255,224,231,.92),rgba(255,255,255,.72) 72%);box-shadow:0 14px 36px rgba(194,51,91,.11),inset 0 1px 0 rgba(255,255,255,.96)}
.overall[data-level="lost"]{border-color:rgba(255,255,255,.8);border-left-color:#c34b67;background:linear-gradient(112deg,rgba(255,225,231,.90),rgba(255,249,250,.69) 72%);box-shadow:0 14px 36px rgba(190,53,83,.12),inset 0 1px 0 rgba(255,255,255,.98)}
.overall[data-level="lost"] h2{color:#9d2946}
/* Glazing adds depth while preserving the crisp measurement layout. */
.card{border:1px solid rgba(255,255,255,.86);border-left:4px solid #8eafb9;border-radius:10px;background:linear-gradient(145deg,rgba(255,255,255,.88),rgba(242,251,252,.68));box-shadow:0 13px 32px rgba(14,72,93,.105),inset 0 1px 0 rgba(255,255,255,.98);backdrop-filter:blur(16px) saturate(1.08);-webkit-backdrop-filter:blur(16px) saturate(1.08);transition:background .32s ease,border-color .32s ease,box-shadow .32s ease}
.card::before{display:block;content:"";position:absolute;z-index:0;top:0;left:0;right:0;height:42%;background:linear-gradient(112deg,rgba(255,255,255,.52),rgba(255,255,255,.12) 47%,rgba(255,255,255,0));pointer-events:none}
.chead,.cbody{position:relative;z-index:1}
.chead{border-bottom:1px solid rgba(255,255,255,.86);background:linear-gradient(180deg,rgba(244,253,253,.72),rgba(235,248,250,.28));box-shadow:inset 0 1px 0 rgba(255,255,255,.8)}
.cbody{background:linear-gradient(180deg,rgba(255,255,255,.06),rgba(238,250,251,.04))}
.card[data-v="ok"]{border-color:rgba(255,255,255,.88);border-left-color:#1a9671;background:linear-gradient(145deg,rgba(255,255,255,.91),rgba(238,252,248,.7))}
.card[data-v="caution"]{border-color:rgba(255,255,255,.88);border-left-color:#ba8428;background:linear-gradient(145deg,rgba(255,250,237,.94),rgba(255,246,228,.72))}
.card[data-v="attention"]{border-color:rgba(255,255,255,.9);border-left-color:#c64c6d;background:linear-gradient(145deg,rgba(255,245,247,.94),rgba(255,232,238,.70))}
.card[data-v="off"]{border-color:rgba(255,255,255,.84);border-left-color:#9bb6be;background:linear-gradient(145deg,rgba(249,253,253,.88),rgba(234,245,247,.68))}
.chead h2{color:#12374a}
.pill{border:1px solid rgba(255,255,255,.8);border-radius:999px;box-shadow:inset 0 1px 0 rgba(255,255,255,.95),0 3px 10px rgba(16,70,90,.05);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px)}
.p-ok{border-color:rgba(66,178,132,.38);background:linear-gradient(140deg,rgba(223,255,240,.94),rgba(202,246,225,.75));color:#075d41}
.p-caution{border-color:rgba(211,156,49,.35);background:linear-gradient(140deg,rgba(255,247,222,.96),rgba(255,230,177,.72));color:#794b00}
.p-attn{border-color:rgba(208,80,112,.34);background:linear-gradient(140deg,rgba(255,239,243,.97),rgba(255,209,221,.76));color:#98263a}
.p-off{border-color:rgba(126,155,166,.28);background:linear-gradient(140deg,rgba(243,250,251,.92),rgba(218,235,240,.68));color:#345566}
/* Dividers dissolve at both ends; measurement values stay sharp and dark. */
.row{border-bottom:0;background-image:linear-gradient(90deg,transparent,rgba(24,84,108,.16) 9%,rgba(24,84,108,.16) 91%,transparent);background-size:100% 1px;background-repeat:no-repeat;background-position:left bottom}
.big{border:1px solid rgba(255,255,255,.88);border-radius:9px;background:linear-gradient(135deg,rgba(237,251,252,.9),rgba(220,245,248,.52));box-shadow:inset 0 1px 0 rgba(255,255,255,.98),0 5px 14px rgba(18,88,109,.055)}
.track{border-radius:999px;box-shadow:inset 0 1px 2px rgba(20,62,76,.12),0 1px 0 rgba(255,255,255,.7)}
.tick,.marker::before{border-radius:2px}
.marker::before{background:var(--vib-accent,#123247);box-shadow:0 0 0 3px rgba(255,255,255,.8),0 2px 7px rgba(18,58,77,.18)}
.history-panel{border:1px solid rgba(255,255,255,.9);border-radius:9px;background:linear-gradient(145deg,rgba(246,254,254,.82),rgba(224,245,248,.58));box-shadow:inset 0 1px 0 rgba(255,255,255,.98),0 5px 16px rgba(15,73,97,.055);transition:background .32s ease,border-color .32s ease}
.history-head strong{color:#12374a}
.history-legend .line-vib{background:var(--vib-accent,#118e9e)}
/* Vibration is state-aware: teal at normal, soft rose at caution, glassy pink at warning. */
#cVib{--vib-accent:#118e9e;--vib-wash:rgba(226,250,251,.78)}
#cVib[data-v="caution"]{--vib-accent:#c36a8a;--vib-wash:rgba(255,241,246,.84);border-color:rgba(227,191,205,.72);background:linear-gradient(145deg,rgba(255,250,251,.95),rgba(255,237,243,.76));box-shadow:0 14px 34px rgba(177,67,105,.09),inset 0 1px 0 rgba(255,255,255,.99)}
#cVib[data-v="caution"] .chead{background:linear-gradient(180deg,rgba(255,245,249,.9),rgba(255,235,242,.53))}
#cVib[data-v="caution"] .big,#cVib[data-v="caution"] .history-panel{background:linear-gradient(145deg,rgba(255,249,251,.97),rgba(255,229,238,.76));border-color:rgba(225,174,193,.55)}
#cVib[data-v="attention"]{--vib-accent:#d33e70;--vib-wash:rgba(255,230,238,.9);border-color:rgba(230,156,179,.7);background:linear-gradient(145deg,rgba(255,247,250,.97),rgba(255,223,233,.86));box-shadow:0 16px 38px rgba(196,49,91,.15),inset 0 1px 0 rgba(255,255,255,.99)}
#cVib[data-v="attention"] .chead{background:linear-gradient(180deg,rgba(255,238,244,.98),rgba(255,222,233,.7));border-bottom-color:rgba(224,143,170,.35)}
#cVib[data-v="attention"] .big{background:linear-gradient(135deg,rgba(255,249,251,.98),rgba(255,217,230,.87));border-color:rgba(225,134,165,.58);box-shadow:inset 0 1px 0 rgba(255,255,255,.99),0 5px 16px rgba(197,55,95,.08)}
#cVib[data-v="attention"] .history-panel{background:linear-gradient(145deg,rgba(255,247,250,.97),rgba(255,226,235,.78));border-color:rgba(223,150,175,.56)}
#cVib[data-v="attention"] .history-head strong,#cVib[data-v="attention"] .history-head span{color:#84344e}
#cVib .vib-chart path[stroke="#128e9a"]{stroke:var(--vib-accent,#118e9e)!important}
#cVib .vib-chart stop[offset="0%"],#cVib .vib-chart stop[offset="100%"]{stop-color:var(--vib-accent,#118e9e)!important}
#cVib .vib-chart circle{fill:var(--vib-accent,#118e9e)!important}
#cVib .history-legend .line-vib{background:var(--vib-accent,#118e9e)}
#cVib .track{box-shadow:inset 0 1px 2px rgba(20,62,76,.10),0 1px 0 rgba(255,255,255,.8)}
/* Connection loss gently tints the whole scene instead of screaming through one red label. */
body.connection-lost{background-image:linear-gradient(180deg,rgba(255,224,231,.34) 0%,rgba(255,240,243,.42) 38%,rgba(249,251,250,.89) 100%),var(--water-photo);background-color:#faedf0}
body.connection-lost .hdr{background-image:linear-gradient(100deg,rgba(255,239,242,.95) 0%,rgba(255,235,239,.77) 39%,rgba(255,229,235,.28) 77%,rgba(121,38,67,.08) 100%),var(--water-photo)}
body.connection-lost .hdr::before{background:linear-gradient(180deg,rgba(255,255,255,.26),rgba(255,231,236,.13) 47%,rgba(171,53,83,.10))}
body.connection-lost .meta{border-color:rgba(255,255,255,.72);background:linear-gradient(135deg,rgba(255,249,250,.82),rgba(255,222,231,.58));box-shadow:inset 0 1px 0 rgba(255,255,255,.98),0 13px 32px rgba(163,48,78,.12)}
body.connection-lost .sim{background:linear-gradient(90deg,rgba(255,226,232,.9),rgba(255,236,240,.66) 52%,rgba(255,249,250,.08) 100%);color:#8c344e}
body.connection-lost .overall{background:linear-gradient(112deg,rgba(255,221,230,.94),rgba(255,248,250,.78) 74%);border-color:rgba(218,125,153,.34);border-left-color:#c34b67;box-shadow:0 15px 38px rgba(177,48,80,.13),inset 0 1px 0 rgba(255,255,255,.98)}
body.connection-lost .overall h2{color:#9d2946}
body.connection-lost .card{border-color:rgba(255,255,255,.84);background:linear-gradient(145deg,rgba(255,251,252,.83),rgba(255,238,242,.63));box-shadow:0 12px 31px rgba(157,48,79,.075),inset 0 1px 0 rgba(255,255,255,.97)}
body.connection-lost .chead{background:linear-gradient(180deg,rgba(255,243,246,.8),rgba(255,233,239,.45))}
body.connection-lost .cbody{background:linear-gradient(180deg,rgba(255,247,249,.14),rgba(255,232,238,.08))}
body.connection-lost .card[data-v="attention"],body.connection-lost #cVib[data-v="attention"]{background:linear-gradient(145deg,rgba(255,243,247,.95),rgba(255,218,230,.82));border-left-color:#c33c69}
/* Buttons, preference fields and capture panels pick up the same restrained glass finish. */
.tgl,.eng-toggle,.btn,.demo button{transition:transform .18s ease,box-shadow .18s ease,background .18s ease,border-color .18s ease}
.tgl,.eng-toggle{border-radius:7px;background:linear-gradient(145deg,rgba(255,255,255,.94),rgba(221,246,249,.76));box-shadow:inset 0 1px 0 rgba(255,255,255,.98),0 4px 12px rgba(10,93,119,.06)}
.btn{border-radius:7px;background:linear-gradient(150deg,#1498b3,#06738f);box-shadow:inset 0 1px 0 rgba(255,255,255,.35),0 7px 16px rgba(8,96,122,.17)}
.ff input{border-radius:7px;box-shadow:inset 0 1px 3px rgba(15,76,97,.05),0 2px 6px rgba(15,76,97,.025)}
.pref,.engineering-values{border-color:rgba(255,255,255,.86);border-radius:9px;background:linear-gradient(145deg,rgba(250,255,255,.88),rgba(230,247,249,.67));box-shadow:inset 0 1px 0 rgba(255,255,255,.98),0 5px 18px rgba(15,73,97,.045)}
.capture-panel{border-color:rgba(222,173,90,.42);border-radius:9px;background:linear-gradient(145deg,rgba(255,248,230,.88),rgba(255,255,255,.70));box-shadow:inset 0 1px 0 rgba(255,255,255,.98),0 7px 20px rgba(139,99,27,.07);backdrop-filter:blur(12px);-webkit-backdrop-filter:blur(12px)}
.capture-empty{border-color:rgba(208,160,77,.55);border-radius:7px;background:linear-gradient(145deg,rgba(255,255,255,.68),rgba(255,246,222,.58))}
.demo{border:1px solid rgba(255,255,255,.84);border-radius:10px;background:linear-gradient(145deg,rgba(251,255,255,.78),rgba(230,247,249,.53));box-shadow:0 10px 28px rgba(15,73,97,.06),inset 0 1px 0 rgba(255,255,255,.98);backdrop-filter:blur(12px);-webkit-backdrop-filter:blur(12px)}
.demo button{border-radius:7px;background:linear-gradient(145deg,rgba(255,255,255,.95),rgba(234,247,249,.72));box-shadow:inset 0 1px 0 rgba(255,255,255,.98)}
.demo button[aria-pressed="true"]{background:linear-gradient(145deg,#1592ab,#08718d);box-shadow:inset 0 1px 0 rgba(255,255,255,.25),0 4px 12px rgba(8,96,122,.12)}
@media(max-width:860px){
 .meta{border-radius:10px}
 #clock{font-size:18px;min-width:130px}
 .overall{border-radius:9px}
 .card{border-radius:9px}
}
@media(max-width:520px){
 .hdr::after{height:2px}
 .meta{padding:8px 9px 8px 11px;gap:7px 9px}
 #clock{font-size:16px;min-width:114px}
 #clock::before{font-size:8px}
 .conn{min-height:37px;padding:7px 10px;font-size:11px}
 .sim{padding-left:var(--gutter);padding-right:var(--gutter)}
 .overall{border-radius:8px}
 .card{border-radius:8px}
 .history-panel{border-radius:8px}
}
@media(prefers-reduced-motion:reduce){.hdr,.meta,.conn,.overall,.card,.history-panel{transition:none!important}}
</style>

</head>
<body>

<div id="sim" class="sim" hidden><strong>SIMULATED DATA.</strong> This page is not connected to the board. Every value below is invented to show the layout. It is not evidence.</div>

<div class="shell">
  <header class="hdr">
    <h1>Pool plant monitor <span id="simTag" class="tag" hidden>SIMULATED</span></h1>
    <div class="meta"><span id="clock">--</span><span id="conn" class="conn"></span></div>
  </header>

  <section id="overall" class="overall" data-level="lost" aria-live="polite">
    <h2 id="ovTitle">Waiting for data</h2>
    <ul id="ovList"></ul>
  </section>

  <main id="grid" class="grid">
    <article id="cVib" class="card" data-v="off"><div class="chead"><h2>Pump vibration</h2><span id="pVib" class="pill p-off">--</span></div><div id="bVib" class="cbody"></div></article>
    <article id="cWat" class="card" data-v="off"><div class="chead"><h2>Water quality</h2><span id="pWat" class="pill p-off">--</span></div><div id="bWat" class="cbody"><div id="wRead"></div><button id="tgl" class="tgl" type="button" aria-expanded="false">Preferences</button><div id="pref" class="pref" hidden><h3>Operator preferences</h3><form id="pf" novalidate><div class="fr"><div class="ff"><label for="tLow">Temperature low (°C)</label><input id="tLow" inputmode="decimal"></div><div class="ff"><label for="tHigh">Temperature high (°C)</label><input id="tHigh" inputmode="decimal"></div></div><div class="fr"><div class="ff"><label for="pPref">Preferred pH (7.2 to 7.8)</label><input id="pPref" inputmode="decimal"></div></div><button class="btn" type="submit" id="pfBtn">Save preferences</button><p id="pfMsg" class="fm" role="status"></p><p class="note">Saved on the board and kept after a restart. A blank field is left unchanged. The pH limits, the calibration and the clear-water baseline cannot be changed here.</p></form></div></div></article>
    <article id="cOcc" class="card" data-v="off"><div class="chead"><h2>Occupancy</h2><span id="pOcc" class="pill p-off">--</span></div><div id="bOcc" class="cbody"></div></article>
    <article id="cSys" class="card" data-v="off"><div class="chead"><h2>System and schedule</h2><span id="pSys" class="pill p-off">--</span></div><div id="bSys" class="cbody"></div></article>
  </main>

  <details id="demo" class="demo" hidden>
    <summary>Demo scenarios</summary>
    <p>Preview only. Each button loads a complete invented reading in the same shape the board sends.</p>
    <div id="demoBtns" class="b"></div>
  </details>

  <p class="foot">This system observes and advises. It does not control pumps, chemical dosing or access. Presence and scene change are not identification.</p>
</div>

<script>
(function(){
'use strict';
var P=new URLSearchParams(location.search);
/* Live only when served by the board (192.168.4.1). Anywhere else the page shows labelled simulated data. ?live is a test hook. */
var LIVE=location.hostname==='192.168.4.1'||P.has('live');
var CFG={pollMs:2000,maxMs:10000,failLimit:3,timeoutMs:3000,statusUrl:'/api/status',cfgUrl:'/api/water/config'};
var $=function(i){return document.getElementById(i);};
var esc=function(s){return String(s).replace(/[&<>"]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c];});};
var num=function(v){return typeof v==='number'&&isFinite(v);};
var fx=function(v,d){return num(v)?v.toFixed(d):null;};
var vibrationSamples=[];
var showEngineeringValues=false;

var REASON={disabled:'Switched off in this firmware build',module_inactive:'Module is not running',not_found:'Sensor not found',
 no_reading_yet:'Waiting for the first reading',invalid_reading:'Reading is invalid',below_valid_range:'Reading is below the valid range',
 above_valid_range:'Reading is above the valid range',stale:'Reading is too old',sensor_error:'Sensor is not responding',
 uart_failed:'Radar data link failed',no_heartbeat_yet:'Camera has not reported yet',vibration_module_disabled:'Vibration module is off in this build',
 calibration_unavailable:'Not calibrated since restart',no_status_yet:'Waiting for a status',no_data:'No data from the board'};
var why=function(r){return r?(REASON[r]||String(r).replace(/_/g,' ')):'No reason given';};
var STATE_WORD={live:'Live',disabled:'Off in this build',unavailable:'Unavailable',stale:'Stale'};
function shown(st,r){return st==='disabled'?why(r):(STATE_WORD[st]||'No data')+': '+why(r);}
function sw(st,r){return st==='disabled'?why(r).toLowerCase():(STATE_WORD[st]||'no data').toLowerCase()+' ('+why(r).toLowerCase()+')';}
var mst=function(m){return !m?'missing':(m.state||'unknown');};

/* ---------- card builders: each returns {v, worst, items, pill, pillCls, html} ---------- */
function row(l,v){return '<div class="row"><span class="rl">'+l+'</span><span class="rv">'+v+'</span></div>';}
function nd(t){return '<span class="nd">'+esc(t)+'</span>';}

function recordVibrationSample(d){
 var m=d&&d.vibration;
 if(m && mst(m)==='live' && num(m.rms)){
  vibrationSamples.push({time:Date.now(),value:m.rms});
  if(vibrationSamples.length>60)vibrationSamples.shift();
 }
}
var boardHist=null;
function vibrationHistoryHtml(m){
 if(boardHist&&boardHist.mean&&boardHist.mean.length>=2)return boardHistoryHtml(m);
 if(!vibrationSamples.length)return '';
 var W=620,H=176,L=47,R=12,T=12,B=29,plotW=W-L-R,plotH=H-T-B;
 var maxV=Math.max(0.8,num(m&&m.warningThreshold)?m.warningThreshold*1.25:0.8);
 vibrationSamples.forEach(function(q){maxV=Math.max(maxV,q.value*1.12);});
 maxV=Math.ceil(maxV*10)/10;
 var x=function(i){return L+(vibrationSamples.length<2?plotW:i*plotW/(vibrationSamples.length-1));};
 var y=function(v){return T+plotH-(Math.max(0,Math.min(maxV,v))/maxV)*plotH;};
 var path=vibrationSamples.map(function(q,i){return (i?'L':'M')+x(i).toFixed(1)+' '+y(q.value).toFixed(1);}).join(' ');
 var area=path+' L '+x(vibrationSamples.length-1).toFixed(1)+' '+(T+plotH)+' L '+L+' '+(T+plotH)+' Z';
 var grid='';
 for(var i=0;i<=4;i++){
  var v=maxV*i/4,yy=y(v);
  grid+='<line x1="'+L+'" y1="'+yy.toFixed(1)+'" x2="'+(W-R)+'" y2="'+yy.toFixed(1)+'" stroke="#dce7ed" stroke-width="1" stroke-dasharray="3 4"/>'+
   '<text x="'+(L-9)+'" y="'+(yy+4).toFixed(1)+'" text-anchor="end" fill="#708596" font-size="11">'+v.toFixed(2)+'</text>';
 }
 var thresholds='';
 [['baseline','#8ba1b0','Baseline'],['cautionThreshold','#d99a22','Caution'],['warningThreshold','#d75d62','Warning']].forEach(function(a){
  if(num(m&&m[a[0]])&&m[a[0]]>=0&&m[a[0]]<=maxV){var yy=y(m[a[0]]);thresholds+='<line x1="'+L+'" y1="'+yy.toFixed(1)+'" x2="'+(W-R)+'" y2="'+yy.toFixed(1)+'" stroke="'+a[1]+'" stroke-width="1.4" stroke-dasharray="5 4" opacity=".9"/>';}
 });
 var last=vibrationSamples[vibrationSamples.length-1];
 var point=vibrationSamples.length===1?'<circle cx="'+x(0)+'" cy="'+y(last.value)+'" r="4.5" fill="#128e9a" stroke="#fff" stroke-width="2"/>':'';
 var firstTime=new Date(vibrationSamples[0].time),lastTime=new Date(last.time);
 var tm=function(d){return (d.getHours()<10?'0':'')+d.getHours()+':'+(d.getMinutes()<10?'0':'')+d.getMinutes()+':'+(d.getSeconds()<10?'0':'')+d.getSeconds();};
 return '<section class="history-panel" aria-label="Vibration history">'+
  '<div class="history-head"><strong>Vibration history</strong><span>'+vibrationSamples.length+' / 60 samples</span></div>'+ 
  '<p class="history-sub">This browser session · not saved to the board</p>'+
  '<svg class="vib-chart" viewBox="0 0 '+W+' '+H+'" role="img" aria-label="Recent RMS vibration trend. Latest value '+last.value.toFixed(3)+'">'+
   '<defs><linearGradient id="vibArea" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="#24a9ab" stop-opacity=".25"/><stop offset="100%" stop-color="#24a9ab" stop-opacity=".025"/></linearGradient></defs>'+grid+thresholds+
   '<path d="'+area+'" fill="url(#vibArea)" stroke="none"/><path d="'+path+'" fill="none" stroke="#128e9a" stroke-width="2.8" stroke-linecap="round" stroke-linejoin="round"/>'+point+
   '<text x="'+L+'" y="'+(H-8)+'" fill="#708596" font-size="11">'+tm(firstTime)+'</text><text x="'+(W-R)+'" y="'+(H-8)+'" text-anchor="end" fill="#708596" font-size="11">'+tm(lastTime)+'</text></svg>'+
  (vibrationSamples.length<2?'<p class="history-hint">Collecting readings — the trend will develop as new samples arrive.</p>':'')+
  '<div class="history-legend"><span><i class="line-vib"></i>RMS</span><span><i class="line-base"></i>Baseline</span><span><i class="line-warn"></i>Warning levels</span></div></section>';
}

/* ---- History stored ON THE BOARD (per minute, in RAM). Used only if /api/vibration/history answers; otherwise the browser-session graph above is used. ---- */
function loadHistory(){
 if(!LIVE)return;
 var ctl=new AbortController(),t=setTimeout(function(){ctl.abort();},5000);
 fetch('/api/vibration/history?minutes=240',{cache:'no-store',signal:ctl.signal})
  .then(function(r){clearTimeout(t);if(!r.ok)throw new Error('http');return r.json();})
  .then(function(h){boardHist=(h&&h.mean&&h.max&&h.mean.length>=2&&h.mean.length===h.max.length)?h:null;})
  .catch(function(){boardHist=null;});
}
function boardHistoryHtml(m){
 var mean=boardHist.mean,mx=boardHist.max,fl=boardHist.flags||[],n=mean.length;
 var W=620,H=176,L=47,R=12,T=12,B=29,plotW=W-L-R,plotH=H-T-B;
 var maxV=Math.max(0.8,num(m&&m.warningThreshold)?m.warningThreshold*1.25:0.8);
 for(var i=0;i<n;i++){if(num(mx[i]))maxV=Math.max(maxV,mx[i]*1.08);}
 maxV=Math.ceil(maxV*10)/10;
 var x=function(i){return L+(n<2?plotW:i*plotW/(n-1));};
 var y=function(v){return T+plotH-(Math.max(0,Math.min(maxV,v))/maxV)*plotH;};
 var pathOf=function(a){var p='',pen=false;for(var i=0;i<n;i++){if(!num(a[i])){pen=false;continue;}p+=(pen?'L':'M')+x(i).toFixed(1)+' '+y(a[i]).toFixed(1);pen=true;}return p;};
 var grid='';
 for(var g=0;g<=4;g++){var v=maxV*g/4,yy=y(v);
  grid+='<line x1="'+L+'" y1="'+yy.toFixed(1)+'" x2="'+(W-R)+'" y2="'+yy.toFixed(1)+'" stroke="#dce7ed" stroke-width="1" stroke-dasharray="3 4"/>'+
   '<text x="'+(L-9)+'" y="'+(yy+4).toFixed(1)+'" text-anchor="end" fill="#708596" font-size="11">'+v.toFixed(2)+'</text>';}
 var thresholds='';
 [['baseline','#8ba1b0'],['cautionThreshold','#d99a22'],['warningThreshold','#d75d62']].forEach(function(a){
  if(num(m&&m[a[0]])&&m[a[0]]>=0&&m[a[0]]<=maxV){var yy=y(m[a[0]]);thresholds+='<line x1="'+L+'" y1="'+yy.toFixed(1)+'" x2="'+(W-R)+'" y2="'+yy.toFixed(1)+'" stroke="'+a[1]+'" stroke-width="1.4" stroke-dasharray="5 4" opacity=".9"/>';}});
 var marks='',bw=Math.max(1.5,plotW/n);
 for(var k=0;k<n;k++){
  if(fl[k]&8)marks+='<rect x="'+(x(k)-bw/2).toFixed(1)+'" y="'+T+'" width="'+bw.toFixed(1)+'" height="'+plotH+'" fill="#8ba1b0" opacity=".22"/>';
  if(fl[k]&2)marks+='<rect x="'+(x(k)-bw/2).toFixed(1)+'" y="'+T+'" width="'+bw.toFixed(1)+'" height="6" fill="#d99a22"/>';}
 return '<section class="history-panel" aria-label="Vibration history stored on the board">'+
  '<div class="history-head"><strong>Vibration history</strong><span>'+n+' min</span></div>'+
  '<p class="history-sub">Stored on the board, one point per minute \u00b7 kept in memory, lost at restart</p>'+
  '<svg class="vib-chart" viewBox="0 0 '+W+' '+H+'" role="img" aria-label="Per-minute RMS vibration history over the last '+n+' minutes">'+
   grid+marks+thresholds+'<path d="'+pathOf(mx)+'" fill="none" stroke="#9cc8cc" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/>'+
   '<path d="'+pathOf(mean)+'" fill="none" stroke="#128e9a" stroke-width="2.8" stroke-linecap="round" stroke-linejoin="round"/>'+
   '<text x="'+L+'" y="'+(H-8)+'" fill="#708596" font-size="11">'+n+' min ago</text><text x="'+(W-R)+'" y="'+(H-8)+'" text-anchor="end" fill="#708596" font-size="11">now</text></svg>'+
  '<div class="history-legend"><span><i class="line-vib"></i>Mean RMS per minute</span><span><i style="background:#9cc8cc"></i>Highest in the minute</span><span><i class="line-base"></i>Baseline</span><span><i class="line-warn"></i>Warning levels</span></div></section>';
}

var VIB={
 'OK':{w:'ok',v:'ok',l:'OK',m:'Vibration is within the normal range.'},
 'CAUTION':{w:'caution',v:'caution',l:'Caution',m:'Vibration is above normal. Keep an eye on the pump.'},
 'WARNING':{w:'attention',v:'attention',l:'Warning',m:'Vibration is high. Check the pump.'},
 'STOPPED':{w:'attention',v:'attention',l:'Stopped',m:'Vibration has stayed very low. The pump appears to be stopped.'},
 'OFF SCHEDULE':{w:'ok',v:'off',l:'Off schedule',m:'Outside the pump window. Vibration is not being judged.'},
 'SENSOR ERROR':{w:'attention',v:'attention',l:'Sensor error',m:'The vibration sensor is not responding.'},
 'DISABLED':{w:'attention',v:'off',l:'Off',m:'This module is switched off in the current firmware build.'}
};
function vibCard(d){
 var m=d.vibration,st=mst(m),r={items:[]};
 if(st==='missing'||st==='unknown'){r.v='attention';r.worst='attention';r.pill='No data';r.pillCls='p-attn';r.items.push('Vibration: the board sent no state');
  r.html='<p class="msg">The board did not report the vibration module.</p>';return r;}
 var meta=VIB[m.status];
 if(st!=='live'){
  var live=st==='stale';
  r.v='attention';r.worst='attention';r.pill=STATE_WORD[st]||st;r.pillCls=st==='disabled'?'p-off':'p-attn';
  r.items.push('Vibration: '+sw(st,m.reason));
  r.html='<p class="msg">'+esc(why(m.reason))+'.</p>'+(live&&num(m.rms)?'<div class="big"><span class="rl">Last RMS</span><b>'+m.rms.toFixed(2)+'</b></div>':'<div class="big"><span class="rl">Current RMS</span><b class="t-off">--</b></div>')+vibrationHistoryHtml(m)+btn();
  return r;}
 if(!meta){meta={w:'attention',v:'attention',l:String(m.status),m:'The board sent a status this page does not recognise.'};}
 r.v=meta.v;r.worst=meta.w;r.pill=meta.l;r.pillCls={ok:'p-ok',caution:'p-caution',attention:'p-attn',off:'p-off'}[meta.v];
 if(meta.w!=='ok')r.items.push('Pump vibration: '+meta.l.toLowerCase());
 var h='<p class="msg">'+esc(meta.m)+'</p><div class="big"><span class="rl">Current RMS</span><b>'+fx(m.rms,2)+'</b></div>';
 if(num(m.baseline)&&m.baseline>0&&num(m.cautionThreshold)&&num(m.warningThreshold)){
  var max=Math.max(m.warningThreshold*1.4,m.rms*1.08),p=function(x){return Math.min(100,Math.max(0,x/max*100));},c=p(m.cautionThreshold),w=p(m.warningThreshold);
  h+='<div class="rl">RMS against baseline</div><div class="track" style="background:linear-gradient(to right,#BFD8D3 0 '+c+'%,#F5D77A '+c+'% '+w+'%,#EBA19B '+w+'% 100%)" role="img" aria-label="RMS '+fx(m.rms,2)+', baseline '+fx(m.baseline,2)+', caution above '+fx(m.cautionThreshold,2)+', warning above '+fx(m.warningThreshold,2)+'">'+
  '<div class="tick" style="left:'+p(m.baseline)+'%"></div><div class="marker" style="left:'+p(m.rms)+'%"></div></div>'+
  '<div class="legend"><span class="lb" style="--c:#12262D">Baseline '+m.baseline.toFixed(2)+'</span><span style="--c:#BFD8D3">Normal</span><span style="--c:#F5D77A">Caution above '+m.cautionThreshold.toFixed(2)+'</span><span style="--c:#EBA19B">Warning above '+m.warningThreshold.toFixed(2)+'</span></div>';
 }else{h+='<p class="msg">No baseline is set, so the reading cannot be compared.</p>';}
 r.html=h+vibrationHistoryHtml(m)+btn();return r;
}
function btn(){return '<button class="btn-off" type="button" disabled>Calibrate baseline: not implemented</button><p class="note">The baseline is set in the firmware build. '+(boardHist&&boardHist.mean&&boardHist.mean.length>=2?'The graph above is stored on the board in memory (one point per minute, up to 24 hours) and is lost at restart.':'The graph above keeps up to 60 readings in this browser session; it is not stored history.')+'</p>';}

function chResult(level,items){return {level:level,items:items};}
function waterCard(d){
 var w=d.water,r={items:[]};
 if(!w){r.v='attention';r.worst='attention';r.pill='No data';r.pillCls='p-attn';r.items.push('Water quality: the board sent no data');r.html='<p class="msg">Water data unavailable.</p>';return r;}
 var rank={ok:0,caution:1,attention:2},worst='ok',items=[];
 function add(l,t){if(rank[l]>rank[worst])worst=l;if(l!=='ok')items.push(t);}
 var simAny=!!w.simulated;
 var html='';
 /* temperature */
 var t=w.temperature||{},ts=mst(t),tv,tst,tcls,tn='';
 if(ts!=='live'){tv=nd('No reading');tst=shown(ts,t.reason);tcls='t-attn';add('attention','Temperature: '+why(t.reason).toLowerCase());}
 else{tv=fx(t.valueC,1)+'<small>\u00B0C</small>';
  var T={IN_RANGE:['In preferred range','t-ok','ok'],BELOW_RANGE:['Below preferred range','t-caution','caution'],ABOVE_RANGE:['Above preferred range','t-caution','caution'],NOT_SET:['Preferred range not set','t-off','ok'],UNAVAILABLE:['Unavailable','t-attn','attention']}[t.status]||[String(t.status),'t-attn','attention'];
  tst=T[0];tcls=T[1];if(T[2]!=='ok')add(T[2],'Temperature: '+T[0].toLowerCase());
  tn=t.rangeSet?'Preferred range: '+fx(t.low,1)+' to '+fx(t.high,1)+' \u00B0C':'Preferred range: not set';}
 html+=ch('Temperature',tv,tst,tcls,tn,t.simulated);
 /* pH */
 var p=w.ph||{},ps=mst(p),pv,pst,pcls,pn=[];
 if(ps!=='live'){pv=nd('No reading');pst=shown(ps,p.reason);pcls='t-attn';add('attention','pH: '+why(p.reason).toLowerCase());}
 else{
  pv=num(p.value)?fx(p.value,2):nd('Not calibrated');
  var dir=(p.direction==='LOW'||p.direction==='HIGH')?' ('+p.direction.toLowerCase()+' side)':'';
  if(p.status==='OK'){pst='Within limits';pcls='t-ok';}
  else if(p.status==='CAUTION'){pst='Caution'+dir;pcls='t-caution';add('caution','pH: caution'+dir);}
  else if(p.status==='WARNING'){pst='Warning'+dir;pcls='t-attn';add('attention','pH: warning'+dir);}
  else{pst='Unavailable: '+why(p.statusReason);pcls='t-attn';add('attention','pH: '+why(p.statusReason).toLowerCase());}
  pn.push({two_point:'Calibration: two-point',partial:'Calibration: partly calibrated',unavailable:'Calibration: not calibrated since restart'}[p.calibration]||'Calibration: unknown');
  if(p.possibleBreach)pn.push('Possible breach, verifying for 30 s');
  var pf=p.preferred||{};
  pn.push(!pf.set?'Preferred pH: not set':pf.status==='ON_TARGET'?'On target (preferred '+fx(pf.value,2)+')':pf.status==='OFF_TARGET'?'Off target (preferred '+fx(pf.value,2)+')':'Preferred pH: unavailable');
  pn.push('Fixed limits 7.2 to 7.8 (set in firmware)');
 }
 html+=ch('pH',pv,pst,pcls,pn.join('<br>'),p.simulated);
 /* turbidity */
 var u=w.turbidity||{},us=mst(u),uv,ust,ucls,un='';
 if(us!=='live'){uv=nd('No reading');ust=shown(us,u.reason);ucls='t-attn';add('attention','Turbidity: '+why(u.reason).toLowerCase());}
 else if(u.status==='NO_BASELINE'||!num(u.percentBelowBaseline)&&u.status!=='CLEAR'&&u.status!=='CLOUDIER'){
  uv=nd('No baseline');ust='No clear-water baseline since restart';ucls='t-attn';add('attention','Turbidity: no clear-water baseline since restart');}
 else{var pc=u.percentBelowBaseline;
  uv=num(pc)?(Math.abs(pc).toFixed(1)+'<small>% '+(pc>=0?'below':'clearer than')+' baseline</small>'):nd('--');
  if(u.status==='CLEAR'){ust='Clear';ucls='t-ok';}else if(u.status==='CLOUDIER'){ust='Cloudier than the clear-water baseline';ucls='t-caution';add('caution','Turbidity: cloudier than baseline');}
  else{ust=String(u.status);ucls='t-attn';add('attention','Turbidity: '+ust);}
  un='Relative to a stored clear-water baseline. Not a measurement in NTU.';}
 html+=ch('Turbidity',uv,ust,ucls,un,u.simulated);
 var ntu=num(u.ntu)?u.ntu:(num(u.valueNtu)?u.valueNtu:(num(u.valueNTU)?u.valueNTU:null));
 var rawV=num(u.rawVolts)?u.rawVolts:null;
 var baseV=num(u.baselineVolts)?u.baselineVolts:null;
 var below=num(u.percentBelowBaseline)?u.percentBelowBaseline:(u.percentBelowBaseline===0?0:null);
 var rawMv=w.ph&&num(w.ph.rawMv)?w.ph.rawMv:null;
 var eng='';
 eng+='<div class="row"><span class="rl">Turbidity (NTU)</span><span class="rv">'+(ntu!==null?ntu.toFixed(2)+'<small>NTU</small>':nd('Not supplied by board'))+'</span></div>';
 eng+='<div class="row"><span class="rl">Turbidity sensor output</span><span class="rv">'+(rawV!==null?rawV.toFixed(3)+'<small>V</small>':nd('No raw voltage'))+'</span></div>';
 eng+='<div class="row"><span class="rl">Clear-water baseline</span><span class="rv">'+(baseV!==null?baseV.toFixed(3)+'<small>V</small>':nd('Not set'))+'</span></div>';
 eng+='<div class="row"><span class="rl">Change from baseline</span><span class="rv">'+(below!==null?below.toFixed(1)+'<small>%</small>':nd('No reading'))+'</span></div>';
 eng+='<div class="row"><span class="rl">pH electrode output</span><span class="rv">'+(rawMv!==null?rawMv.toFixed(1)+'<small>mV</small>':nd('Not supplied'))+'</span></div>';
 html+='<div class="engineering-block"><button class="eng-toggle" type="button" data-engineering-toggle aria-expanded="'+String(showEngineeringValues)+'">'+(showEngineeringValues?'Hide engineering values':'Show engineering values')+'</button><div class="engineering-values"'+(showEngineeringValues?'':' hidden')+'>'+eng+'<p class="engineering-note">NTU is shown only when the board provides a calibrated NTU value. A voltage or percentage change from the clear-water baseline cannot safely be converted to NTU without sensor calibration.</p></div></div>';
  r.v=worst==='ok'?'ok':worst;r.worst=worst;r.items=items;
 r.pill=worst==='ok'?'All normal':worst==='caution'?'Caution':'Attention';r.pillCls={ok:'p-ok',caution:'p-caution',attention:'p-attn'}[worst];
 r.html=(simAny?'<p class="msg"><span class="tag">BOARD TEST MODE</span> Some readings are simulated by the board itself.</p>':'')+html;return r;
}
function ch(n,v,s,c,note,sim){return '<div class="ch"><div class="ch-top"><span class="ch-name">'+n+(sim?'<span class="tag">SIMULATED</span>':'')+'</span><span class="ch-val">'+v+'</span></div><p class="ch-st '+c+'">'+esc(s)+'</p>'+(note?'<p class="ch-note">'+note+'</p>':'')+'</div>';}
function eventCaptureSource(oc){
 var candidates=[oc&&oc.captureDataUrl,oc&&oc.captureUrl,oc&&oc.imageUrl,oc&&oc.snapshotUrl];
 for(var i=0;i<candidates.length;i++){
  var v=candidates[i];
  if(typeof v==='string' && (/^data:image\/(?:jpeg|jpg|png|webp);base64,/i.test(v) || /^https?:\/\//i.test(v) || /^\/(?!\/)/.test(v)))return v;
 }
 return null;
}
function eventCaptureHtml(oc){
 var src=eventCaptureSource(oc);
 var inner=src?'<img class="capture-image" src="'+esc(src)+'" alt="Camera image captured during the high-change and radar event" loading="lazy"><p class="capture-caption">Image supplied by the board for this event.</p>':
  '<div class="capture-empty"><span class="capture-symbol">▧</span><div><strong>Capture required</strong><p>High scene change and radar presence coincide. The current board response does not include an image yet.</p></div></div><p class="capture-caption">Camera firmware/API must return a capture image before it can appear here.</p>';
 return '<section class="capture-panel" aria-label="Event camera capture"><div class="capture-head"><strong>Event capture</strong><span class="capture-trigger">Trigger condition met</span></div>'+inner+'</section>';
}
function occCard(d){
 var rd=d.radar,oc=d.occupancy,r={items:[]},worst='ok',rank={ok:0,caution:1,attention:2};
 function add(l,t){if(rank[l]>rank[worst])worst=l;if(l!=='ok')r.items.push(t);}
 var rs=mst(rd),os=mst(oc),h='';
 var radarLive=rs==='live';
 if(radarLive){
  h+=row('Radar presence',rd.presence?'<span class="dot live"></span>Detected':'<span class="dot hollow"></span>Clear');
  h+=row('Distance',rd.presence&&num(rd.distanceCm)?rd.distanceCm+'<small>cm</small>'+(rd.moving?'<small>moving</small>':rd.stationary?'<small>still</small>':''):nd('\u2013'));
 }else{
  h+=row('Radar presence',nd(shown(rs,rd&&rd.reason)));
  add('attention','Radar: '+sw(rs,rd&&rd.reason));
 }
 var camLive=os==='live';
 var age=oc&&num(oc.heartbeatAgeMs)?Math.round(oc.heartbeatAgeMs/1000):null;
 if(camLive){h+=row('Camera link','<span class="dot live"></span>Online'+(age!==null?'<small>heard '+age+' s ago</small>':''));}
 else{h+=row('Camera link','<span class="dot hollow"></span><span class="t-attn">'+(os==='stale'?'Offline':(STATE_WORD[os]||'No data'))+'</span>'+(age!==null?'<small>last heard '+age+' s ago</small>':''));
  add('attention','Camera: '+(os==='stale'?'offline, no heartbeat for '+age+' s':why(oc&&oc.reason).toLowerCase()));}
 if(camLive&&num(oc.changePercent)){h+=row('Scene change',(oc.eventActive?'<span class="t-caution">':'<span>')+oc.changePercent.toFixed(1)+'<small>%</small></span>');}
 else{h+=row('Scene change',nd(camLive?'No value':'No data'));}
 h+=row('Detection events',num(oc&&oc.eventCount)?String(oc.eventCount):nd('No data'));
 h+=row('Last event',oc&&oc.lastEvent?'<span style="font-size:16px">'+esc(oc.lastEvent)+'</span>':nd('None since start'));
 var line='';
 if(radarLive&&camLive){
  if(rd.presence&&oc.eventActive)line='Presence detected and scene change observed. Radar and camera agree.';
  else if(rd.presence)line='Presence detected by radar only. No scene change observed.';
 }
 if(line)h+='<p class="msg" style="color:var(--caution);font-weight:700">'+esc(line)+'</p>';
 if(radarLive&&rd.presence===true&&camLive&&oc.eventActive===true)h+=eventCaptureHtml(oc);
 h+='<p class="note">Presence is not identification, and scene change alone does not identify a person. Event capture is displayed only when an image is supplied by the board.</p>';
 r.html=h;r.worst=worst;
 var act=radarLive&&rd.presence;
 r.v=worst==='attention'?'attention':act?'caution':'ok';
 r.pill=worst==='attention'?'Attention':act?'Activity':'All clear';r.pillCls=worst==='attention'?'p-attn':act?'p-caution':'p-ok';
 return r;
}

function sysCard(d){
 var r={items:[]},worst='ok',rank={ok:0,caution:1,attention:2},h='';
 function add(l,t){if(rank[l]>rank[worst])worst=l;if(l!=='ok')r.items.push(t);}
 var rt=d.rtc,rs=mst(rt);
 if(rs==='live')h+=row('Clock',esc(rt.time||'--'));
 else{h+=row('Clock',nd(shown(rs,rt&&rt.reason)));add('attention','Clock: '+sw(rs,rt&&rt.reason));}
 var sc=d.schedule,ss=mst(sc);
 if(ss==='live'){
  var p2=function(n){return (n<10?'0':'')+n;};
  h+=row('Pump window',sc.windowSet&&num(sc.startHour)?p2(sc.startHour)+':'+p2(sc.startMinute)+' to '+p2(sc.endHour)+':'+p2(sc.endMinute):nd('Not set'));
  h+=row('Inside the window now',sc.monitoringNow===true?'<span class="dot live"></span>Yes':sc.monitoringNow===false?'<span class="dot hollow"></span>No':nd('Unknown'));
 }else{h+=row('Pump window',nd(shown(ss,sc&&sc.reason)));}
 h+='<p class="note">The pump window is read-only here. It is set by a serial command in builds without water quality. The board cannot tell if the clock is hours out.</p>';
 h+='<div class="mods"><h3>Modules</h3><ul>';
 var L=[['Vibration',d.vibration],['Clock',d.rtc],['Radar',d.radar],['Camera link',d.occupancy],['Temperature',d.water&&d.water.temperature],['pH',d.water&&d.water.ph],['Turbidity',d.water&&d.water.turbidity]];
 L.forEach(function(x){var s=mst(x[1]),cls=s==='live'?'live':s==='stale'?'amber':s==='disabled'?'grey':'warn',word=s==='live'?'Live':(STATE_WORD[s]||'No data');
  if(s==='live'&&x[0]==='pH'&&x[1].status==='UNAVAILABLE'){cls='amber';word='Not calibrated';}
  if(s==='live'&&x[0]==='Turbidity'&&x[1].status==='NO_BASELINE'){cls='amber';word='No baseline';}
  h+='<li><span>'+x[0]+'</span><span><span class="dot '+cls+'"></span>'+word+'</span></li>';});
 h+='</ul></div>';
 r.html=h;r.worst=worst;r.v=worst==='ok'?'ok':'attention';r.pill=worst==='ok'?'Clock OK':'Attention';r.pillCls=worst==='ok'?'p-ok':'p-attn';
 return r;
}

/* ---------- overall status (approved rule) ---------- */
function overall(cards){
 var rank={ok:0,caution:1,attention:2},w='ok',items=[];
 cards.forEach(function(c){if(rank[c.worst]>rank[w])w=c.worst;c.items.forEach(function(i){items.push(i);});});
 return {level:w,items:items};
}

var lost=false,lastData=null,lastOk=null;
function paint(c,pill,body,card){
 $(card).setAttribute('data-v',c.v);$(pill).className='pill '+c.pillCls;$(pill).textContent=c.pill;
 $(body==='bWat'?'wRead':body).innerHTML=c.html;
}
function safe(fn,d,name){try{return fn(d);}catch(e){return {v:'attention',worst:'attention',items:[name+': this card could not be drawn'],pill:'Error',pillCls:'p-attn',html:'<p class="msg">This card could not be drawn from the data received.</p>'};}}
function render(d,recordSample){
 lastData=d;
 if(recordSample!==false)recordVibrationSample(d);
 var c=[safe(vibCard,d,'Vibration'),safe(waterCard,d,'Water quality'),safe(occCard,d,'Occupancy'),safe(sysCard,d,'System')];
 paint(c[0],'pVib','bVib','cVib');paint(c[1],'pWat','bWat','cWat');paint(c[2],'pOcc','bOcc','cOcc');paint(c[3],'pSys','bSys','cSys');
 var engToggle=document.querySelector('[data-engineering-toggle]');
 if(engToggle)engToggle.onclick=function(){showEngineeringValues=!showEngineeringValues;if(lastData)render(lastData,false);};
 var o=overall(c);
 $('overall').setAttribute('data-level',o.level);
 $('ovTitle').textContent=o.level==='ok'?'All monitored systems normal':o.level==='caution'?'Caution':'Attention needed';
 $('ovList').innerHTML=o.level==='ok'?'<li>Every module is live and within range.</li>':o.items.map(function(i){return '<li>'+esc(i)+'</li>';}).join('');
 var rt=d.rtc;$('clock').textContent=rt&&mst(rt)==='live'?rt.time:'Clock unavailable';
 setLost(false);
}
function setLost(l){
 lost=l;document.body.classList.toggle('connection-lost',l);$('grid').classList.toggle('is-lost',l);
 var cn=$('conn');cn.className='conn '+(l?'bad':'ok');
 cn.innerHTML=l?'<span class="dot hollow"></span>Not connected':'<span class="dot live"></span>Live';
 if(l){$('overall').setAttribute('data-level','lost');$('ovTitle').textContent='No connection to the board';
  var s=lastOk?Math.round((Date.now()-lastOk)/1000):null;
  $('ovList').innerHTML='<li>'+(lastData?'The readings below are the last ones received'+(s!==null?', '+s+' s ago':'')+'. Nothing is being updated.':'No readings have been received yet.')+'</li>';}
}

/* ---------- preferences form ---------- */
var demoCfg={tempSet:true,tempLow:20,tempHigh:30,phSet:true,phPref:7.5};
function bindPref(){
 var t=$('tgl');
 t.onclick=function(){var h=$('pref').hidden;$('pref').hidden=!h;t.setAttribute('aria-expanded',String(h));if(h)loadCfg();};
 $('pf').onsubmit=savePref;
}
function fillCfg(c){$('tLow').value=c.tempSet?c.tempLow:'';$('tHigh').value=c.tempSet?c.tempHigh:'';$('pPref').value=c.phSet?c.phPref:'';}
function loadCfg(){
 if(!LIVE){fillCfg(demoCfg);return;}
 fetch(CFG.cfgUrl,{cache:'no-store'}).then(function(r){return r.json();}).then(fillCfg).catch(function(){msg('Could not load the current preferences.','err');});
}
function msg(t,k){var m=$('pfMsg');if(m){m.textContent=t;m.className='fm'+(k?' '+k:'');}}
function showReply(j){
 if(j&&j.ok){msg(!j.changed?'No changes.':j.saved?'Saved.':'Applied, but could not be saved to flash.',j.changed&&!j.saved?'err':'ok');}
 else{msg('Not saved: '+((j&&j.errors&&j.errors.length)?j.errors.join(' '):'refused by the board.'),'err');}
}
function savePref(e){
 e.preventDefault();var b=$('pfBtn');b.disabled=true;b.textContent='Saving';
 var f={tempLow:$('tLow').value.trim(),tempHigh:$('tHigh').value.trim(),phPref:$('pPref').value.trim()};
 var done=function(j){b.disabled=false;b.textContent='Save preferences';showReply(j);};
 if(!LIVE){setTimeout(function(){demoSave(f,done);},350);return;}
 fetch(CFG.cfgUrl,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(f)})
  .then(function(r){return r.json();}).then(done).catch(function(){b.disabled=false;b.textContent='Save preferences';msg('Not saved: no response from the board.','err');});
}

/* ---------- polling ---------- */
var fails=0,delay=CFG.pollMs;
function get(){
 if(!LIVE)return demoFetch();
 var ctl=new AbortController(),t=setTimeout(function(){ctl.abort();},CFG.timeoutMs);
 return fetch(CFG.statusUrl,{cache:'no-store',signal:ctl.signal}).then(function(r){clearTimeout(t);if(!r.ok)throw new Error('http');return r.json();});
}
function poll(){
 get().then(function(d){fails=0;delay=CFG.pollMs;lastOk=Date.now();render(d);})
 .catch(function(){fails++;if(fails>=CFG.failLimit){setLost(true);delay=Math.min(CFG.maxMs,delay*2);}else{delay=CFG.pollMs;}})
 .then(function(){setTimeout(poll,delay);});
}

/* ======================================================================
   SIMULATED DATA. Used only when the page is NOT served by the board.
   Same field names and shapes as the board's /api/status.
====================================================================== */
var scen='normal';
function two(n){return (n<10?'0':'')+n;}
function nowStr(){var n=new Date();return n.getFullYear()+'-'+two(n.getMonth()+1)+'-'+two(n.getDate())+' '+two(n.getHours())+':'+two(n.getMinutes())+':'+two(n.getSeconds());}
function jit(x,a){return x+(Math.random()-.5)*a;}
function liveS(age){return {state:'live',reason:null,ageMs:age};}
function base(){
 var rms=jit(0.5352,0.02);
 return {
  vibration:{state:'live',reason:null,ageMs:22,status:'OK',rms:rms,baseline:0.5356,cautionThreshold:0.6427,warningThreshold:0.8034},
  rtc:{state:'live',reason:null,ageMs:null,ready:true,time:nowStr()},
  radar:{state:'live',reason:null,ageMs:40,presence:false,distanceCm:0,moving:false,stationary:false},
  occupancy:{state:'live',reason:null,ageMs:900,moduleOnline:true,eventCount:3,eventActive:false,changePercent:jit(1.3,0.5),lastEvent:'2026-10-24 02:14:37',heartbeatAgeMs:900},
  schedule:{state:'live',reason:null,ageMs:null,windowSet:true,startHour:6,startMinute:0,endHour:18,endMinute:0,monitoringNow:true},
  water:{active:true,simulated:false,
   temperature:{enabled:true,state:'live',reason:null,simulated:false,valueC:jit(26.8,0.2),ageMs:116,rangeSet:true,low:20.0,high:30.0,statusNow:'IN_RANGE',status:'IN_RANGE'},
   ph:{enabled:true,state:'live',reason:null,simulated:false,rawMv:null,ageMs:6,calibration:'two_point',calibrationAgeS:1800,value:jit(7.46,0.03),status:'OK',statusReason:null,direction:null,zoneNow:'OK',possibleBreach:false,tempCompensation:null,preferred:{set:true,value:7.5,deviation:0.04,status:'ON_TARGET'}},
   turbidity:{enabled:true,state:'live',reason:null,simulated:false,rawVolts:null,ageMs:7,baseline:'set',baselineVolts:3.42,percentBelowBaseline:jit(0.8,0.3),statusNow:'CLEAR',status:'CLEAR'},
   settings:{restoredFromFlash:true}}
 };
}
var SC={
 normal:['Normal',function(d){}],
 warning:['Vibration warning',function(d){d.vibration.status='WARNING';d.vibration.rms=jit(0.91,0.03);}],
 caution:['Vibration caution',function(d){d.vibration.status='CAUTION';d.vibration.rms=jit(0.70,0.02);}],
 stopped:['Pump stopped',function(d){d.vibration.status='STOPPED';d.vibration.rms=jit(0.3459,0.004);}],
 offsched:['Off schedule',function(d){d.vibration.status='OFF SCHEDULE';d.schedule.monitoringNow=false;}],
 sensor:['Vibration sensor error',function(d){d.vibration.state='unavailable';d.vibration.reason='sensor_error';d.vibration.status='SENSOR ERROR';}],
 disabled:['Modules off (bench build)',function(d){
  d.vibration={state:'disabled',reason:'disabled',ageMs:null,status:'DISABLED',rms:0,baseline:0.5356,cautionThreshold:0.6427,warningThreshold:0.8034};
  d.rtc={state:'disabled',reason:'disabled',ageMs:null,ready:false,time:'RTC unavailable'};
  d.radar={state:'disabled',reason:'disabled',ageMs:null,presence:null,distanceCm:null,moving:null,stationary:null};
  d.schedule={state:'disabled',reason:'vibration_module_disabled',ageMs:null,windowSet:false,monitoringNow:null};}],
 restart:['After a restart (uncalibrated)',function(d){
  d.water.ph.calibration='unavailable';d.water.ph.calibrationAgeS=null;d.water.ph.value=null;d.water.ph.status='UNAVAILABLE';d.water.ph.statusReason='calibration_unavailable';d.water.ph.zoneNow=null;d.water.ph.preferred={set:true,value:7.5,deviation:null,status:'UNAVAILABLE'};
  d.water.turbidity.baseline='unavailable';d.water.turbidity.baselineVolts=null;d.water.turbidity.percentBelowBaseline=null;d.water.turbidity.statusNow=null;d.water.turbidity.status='NO_BASELINE';}],
 probe:['Temperature probe not found',function(d){var t=d.water.temperature;t.state='unavailable';t.reason='not_found';t.valueC=null;t.ageMs=null;t.statusNow=null;t.status='UNAVAILABLE';}],
 phwarn:['pH warning, high side',function(d){var p=d.water.ph;p.value=7.86;p.status='WARNING';p.direction='HIGH';p.zoneNow='WARNING';p.preferred={set:true,value:7.5,deviation:0.36,status:'OFF_TARGET'};}],
 cloudy:['Water cloudier',function(d){var u=d.water.turbidity;u.percentBelowBaseline=7.4;u.status='CLOUDIER';u.statusNow='CLOUDIER';}],
 event:['Occupancy event',function(d){d.radar.presence=true;d.radar.distanceCm=Math.round(jit(112,8));d.radar.moving=true;d.occupancy.eventActive=true;d.occupancy.changePercent=jit(46,4);d.occupancy.eventCount=4;d.occupancy.lastEvent=nowStr();}],
 camoff:['Camera offline',function(d){var o=d.occupancy;o.state='stale';o.reason='stale';o.ageMs=47000;o.moduleOnline=false;o.eventActive=null;o.changePercent=null;o.heartbeatAgeMs=47000;}],
 lost:['Connection lost',function(d){}]
};
function demoFetch(){return new Promise(function(res,rej){if(scen==='lost'){rej(new Error('lost'));return;}var d=base();SC[scen][1](d);res(d);});}
function demoSave(f,done){
 var errs=[],tl=f.tempLow,th=f.tempHigh,pp=f.phPref,ok=/^[+-]?(\d+\.?\d*|\.\d+)$/;
 if((tl===''||th==='')&&!(tl===''&&th===''))errs.push('Temperature needs both a low and a high value.');
 else if(tl!==''){if(!ok.test(tl)||!ok.test(th))errs.push('Temperature values must be numbers.');else if(+tl>=+th)errs.push('Temperature low must be below high.');}
 if(pp!==''){if(!ok.test(pp))errs.push('Preferred pH must be a number.');else if(+pp<7.2||+pp>7.8)errs.push('Preferred pH must be between 7.2 and 7.8.');}
 if(errs.length){done({ok:false,changed:false,saved:false,errors:errs});return;}
 var ch=false;
 if(tl!==''&&(+tl!==demoCfg.tempLow||+th!==demoCfg.tempHigh)){demoCfg.tempLow=+tl;demoCfg.tempHigh=+th;demoCfg.tempSet=true;ch=true;}
 if(pp!==''&&+pp!==demoCfg.phPref){demoCfg.phPref=+pp;demoCfg.phSet=true;ch=true;}
 done({ok:true,changed:ch,saved:ch,errors:[]});
}
function initDemo(){
 $('sim').hidden=false;$('simTag').hidden=false;$('demo').hidden=false;document.title='SIMULATED: Pool plant monitor';
 var h='';Object.keys(SC).forEach(function(k){h+='<button type="button" data-k="'+k+'" aria-pressed="'+(k===scen)+'">'+SC[k][0]+'</button>';});
 $('demoBtns').innerHTML=h;
 $('demoBtns').addEventListener('click',function(e){var b=e.target.closest('button');if(!b)return;scen=b.getAttribute('data-k');
  Array.prototype.forEach.call($('demoBtns').children,function(x){x.setAttribute('aria-pressed',String(x===b));});
  if(scen==='lost'){fails=CFG.failLimit;setLost(true);}else{demoFetch().then(function(d){lastOk=Date.now();render(d);});}});
}

bindPref();
if(!LIVE)initDemo();
if(LIVE){loadHistory();setInterval(loadHistory,30000);}
setLost(true);
poll();
})();
</script>
</body>
</html>

)HTML";

static String boolJson(bool v) { return v ? "true" : "false"; }

static void handleRoot() {
  networkServer().send_P(200, "text/html", DASHBOARD_HTML);
}

// Same state/reason/ageMs pattern as the water channels. reason and ageMs are null when not meaningful.
static String stateFields(const char* state, const char* reason, long ageMs) {
  String s = "\"state\":\"" + String(state) + "\",\"reason\":";
  s += reason ? "\"" + String(reason) + "\"" : String("null");
  s += ",\"ageMs\":" + (ageMs >= 0 ? String(ageMs) : String("null"));
  return s;
}

static String numOrNull(bool live, String v) { return live ? v : String("null"); }

// 3 x the ~22 ms read interval is below the 2000 ms floor, so the floor applies.
static const long VIB_STALE_MS = 2000;

static String vibrationJson() {
  String status = vibrationGetStatus(), st;
#if CFG_VIBRATION_CONNECTED
  long age = vibrationGetLastGoodAgeMs();
  if (status == "SENSOR ERROR") st = stateFields("unavailable", "sensor_error", age);
  else if (age > VIB_STALE_MS)  st = stateFields("stale", "stale", age);
  else                          st = stateFields("live", nullptr, age);
#else
  status = "DISABLED";
  st = stateFields("disabled", "disabled", -1);
#endif
  return "{" + st + ",\"status\":\"" + status + "\"," +
    "\"rms\":" + String(vibrationGetRMS(), 4) + ",\"baseline\":" + String(vibrationGetBaseline(), 4) +
    ",\"cautionThreshold\":" + String(vibrationGetCautionThreshold(), 4) +
    ",\"warningThreshold\":" + String(vibrationGetWarningThreshold(), 4) + "}";
}

static String rtcJson() {
  String st = !CFG_RTC_CONNECTED ? stateFields("disabled", "disabled", -1)
            : scheduleIsReady()  ? stateFields("live", nullptr, -1)
                                 : stateFields("unavailable", "not_found", -1);
  return "{" + st + ",\"ready\":" + boolJson(scheduleIsReady()) +
         ",\"time\":\"" + scheduleGetTimeString() + "\"}";
}

static String radarJson() {
  bool live = false;
  String st;
#if CFG_RADAR_CONNECTED
  if (!radarUartReady())       st = stateFields("unavailable", "uart_failed", -1);
  else if (!radarFrameFresh()) st = stateFields("stale", "stale", radarLastFrameAgeMs());
  else { st = stateFields("live", nullptr, radarLastFrameAgeMs()); live = true; }
#else
  st = stateFields("disabled", "disabled", -1);
#endif
  return "{" + st + ",\"presence\":" + numOrNull(live, boolJson(radarPresenceDetected())) +
         ",\"distanceCm\":" + numOrNull(live, String(radarDistanceCm())) +
         ",\"moving\":" + numOrNull(live, boolJson(radarMoving())) +
         ",\"stationary\":" + numOrNull(live, boolJson(radarStationary())) + "}";
}

static String occupancyJson() {
  bool never = lastOccupancyHeartbeat == 0;
  long age = never ? -1 : (long)(millis() - lastOccupancyHeartbeat);
  bool live = !never && age < 10000;
  String st = never ? stateFields("unavailable", "no_heartbeat_yet", -1)
            : live  ? stateFields("live", nullptr, age)
                    : stateFields("stale", "stale", age);
  String le = dashboardOccupancyLastEventTime();
  // Count and last event are kept when stale; eventActive and changePercent are only meaningful live.
  return "{" + st + ",\"moduleOnline\":" + boolJson(live) +
         ",\"eventCount\":" + numOrNull(!never, String(dashboardOccupancyEventCount())) +
         ",\"eventActive\":" + numOrNull(live, boolJson(dashboardOccupancyEventActive())) +
         ",\"changePercent\":" + numOrNull(live, String(dashboardOccupancyChangePercent(), 1)) +
         ",\"lastEvent\":" + (le.length() ? "\"" + le + "\"" : String("null")) +
         ",\"heartbeatAgeMs\":" + (age >= 0 ? String(age) : String("null")) + "}";
}

static String scheduleJson() {
#if CFG_VIBRATION_CONNECTED
  bool set = vibrationPumpWindowSet();
  int w = vibrationPumpWindowState();
  int sm = vibrationPumpStartMinute(), em = vibrationPumpEndMinute();
  return "{" + stateFields("live", nullptr, -1) + ",\"windowSet\":" + boolJson(set) +
         ",\"startHour\":" + numOrNull(set, String(sm / 60)) + ",\"startMinute\":" + numOrNull(set, String(sm % 60)) +
         ",\"endHour\":" + numOrNull(set, String(em / 60)) + ",\"endMinute\":" + numOrNull(set, String(em % 60)) +
         ",\"monitoringNow\":" + (w < 0 ? String("null") : boolJson(w == 1)) + "}";
#else
  return "{" + stateFields("disabled", "vibration_module_disabled", -1) + ",\"windowSet\":false,\"startHour\":null,\"startMinute\":null,\"endHour\":null,\"endMinute\":null,\"monitoringNow\":null}";
#endif
}

static void handleStatus() {
  String json;
  json.reserve(2048);
  json = "{\"vibration\":" + vibrationJson() + ",\"rtc\":" + rtcJson() +
         ",\"radar\":" + radarJson() + ",\"occupancy\":" + occupancyJson() +
         ",\"schedule\":" + scheduleJson() + ",\"water\":" + waterQualityStatusJson() + "}";
  networkServer().sendHeader("Cache-Control", "no-store");
  networkServer().send(200, "application/json", json);
}

static void handleOccupancyHeartbeat() {
  lastOccupancyHeartbeat = millis();
  occupancySenderIP = networkServer().client().remoteIP().toString();

  int incomingCount = networkServer().arg("eventCount").toInt();
  occupancyEventActive = (networkServer().arg("eventActive") == "1");
  occupancyChangePercent = networkServer().arg("changePercent").toFloat();

  if (incomingCount < occupancyEventCount) {
    Serial.println("[Occ] camera restart detected");
    occupancyLastEventTime = "";
  }

  if (incomingCount > occupancyEventCount) {
    occupancyLastEventTime = scheduleGetTimeString();
    Serial.print("[Occ] event #");
    Serial.print(incomingCount);
    Serial.print(" at ");
    Serial.println(occupancyLastEventTime);
  }

  occupancyEventCount = incomingCount;

  if (millis() - lastOccPrint >= 5000) {
    lastOccPrint = millis();
    Serial.print("[Occ] online ip=");
    Serial.print(occupancySenderIP);
    Serial.print(" count=");
    Serial.print(occupancyEventCount);
    Serial.print(" active=");
    Serial.print(occupancyEventActive ? "1" : "0");
    Serial.print(" chg=");
    Serial.print(occupancyChangePercent, 1);
    Serial.print(" last=");
    Serial.println(occupancyLastEventTime);
  }

  networkServer().send(200, "application/json", "{\"ok\":true}");
}

// Strict decimal parse: digits, sign, point and exponent only; no empty text, NaN, inf or trailing text.
static bool parseStrictFloat(const String& s, float* out) {
  if (s.length() == 0) return false;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E')) return false;
  }
  char* end = nullptr;
  double d = strtod(s.c_str(), &end);
  if (end == s.c_str() || *end != '\0') return false;
  float f = (float)d;
  if (isnan(f) || isinf(f)) return false;
  *out = f;
  return true;
}

// Reads one form field. Blank means "unchanged" (present stays false); bad text adds an error.
static void readConfigField(const char* name, const char* label, bool* present, float* value, String* errors) {
  *present = false;
  String s = networkServer().arg(name);
  s.trim();
  if (s.length() == 0) return;
  if (!parseStrictFloat(s, value)) {
    *errors += String(label) + " is not a valid number.\n";
    return;
  }
  *present = true;
}

static void handleWaterConfigGet() {
  networkServer().sendHeader("Cache-Control", "no-store");
  networkServer().send(200, "application/json", waterQualityConfigJson());
}

static void handleWaterConfigPost() {
  String errors;
  bool hasLow, hasHigh, hasPh;
  float low = NAN, high = NAN, ph = NAN;
  readConfigField("tempLow", "Temperature minimum", &hasLow, &low, &errors);
  readConfigField("tempHigh", "Temperature maximum", &hasHigh, &high, &errors);
  readConfigField("phPref", "Preferred pH", &hasPh, &ph, &errors);
  if (!hasLow) low = NAN;
  if (!hasHigh) high = NAN;

  WaterConfigResult r = waterQualityApplyConfig(hasLow || hasHigh, low, high, hasPh, ph, &errors);

  String json = "{\"ok\":" + boolJson(r.ok) + ",\"changed\":" + boolJson(r.changed) + ",\"saved\":" + boolJson(r.saved) + ",\"errors\":[";
  bool first = true;
  int start = 0;
  while (start < (int)errors.length()) {
    int nl = errors.indexOf('\n', start);
    if (nl < 0) nl = errors.length();
    String line = errors.substring(start, nl);
    start = nl + 1;
    if (line.length() == 0) continue;
    line.replace("\\", "\\\\");
    line.replace("\"", "\\\"");
    if (!first) json += ",";
    json += "\"" + line + "\"";
    first = false;
  }
  json += "]}";

  networkServer().sendHeader("Cache-Control", "no-store");
  networkServer().send(r.ok ? 200 : 400, "application/json", json);
}

void dashboardSetup() {
  networkServer().on("/api/water/config", HTTP_GET, handleWaterConfigGet);
  networkServer().on("/api/water/config", HTTP_POST, handleWaterConfigPost);
  networkServer().on("/", HTTP_GET, handleRoot);
  networkServer().on("/api/status", HTTP_GET, handleStatus);
  networkServer().on("/api/occupancy/heartbeat", HTTP_GET, handleOccupancyHeartbeat);

  Serial.println("Dashboard routes registered.");
}

void dashboardUpdate() {
}

bool dashboardOccupancyOnline() {
  return lastOccupancyHeartbeat != 0 && (millis() - lastOccupancyHeartbeat < 10000);
}

int dashboardOccupancyEventCount() {
  return occupancyEventCount;
}

bool dashboardOccupancyEventActive() {
  return occupancyEventActive;
}

float dashboardOccupancyChangePercent() {
  return occupancyChangePercent;
}

String dashboardOccupancySenderIP() {
  return occupancySenderIP;
}

String dashboardOccupancyLastEventTime() {
  return occupancyLastEventTime;
}
