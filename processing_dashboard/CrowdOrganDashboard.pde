import oscP5.*;
import netP5.*;
import java.util.*;

// This Processing sketch functions as a live oscilloscope for the gesture
// system. We keep the code intentionally narrative: every draw step explains
// what metric is being visualized so operators can tweak mappings mid-show.

OscP5 osc;
NetAddress hostControl;

class Voice {
  boolean active = false;
  float x, y, z;
  float size;
  float motion;
  float energy;
  float note;
  float velocity;
  String lastGestureType = "";
  float lastGestureStrength = 0.0;
  int lastGestureFrame = -999;
}

class ZoneFlash {
  int camId;
  String type;
  float strength;
  int zoneIndex; // -1 for sweeps
  int startFrame;
  int lifespan = 180;
}

class GestureLogEntry {
  String scope;
  String label;
  String type;
  float strength;
  String detail;
  int frame;
}

class CameraDisplayCalibration {
  String label = "";
  int cols = 0;
  int rows = 0;
  HashSet<Integer> ignoredZones = new HashSet<Integer>();
  HashMap<Integer, String> zoneLabels = new HashMap<Integer, String>();
}

HashMap<Integer, Voice> voices = new HashMap<Integer, Voice>();

float globalMotion = 0.0;

HashMap<Integer, Integer> camCols = new HashMap<Integer, Integer>();
HashMap<Integer, Integer> camRows = new HashMap<Integer, Integer>();
HashMap<Integer, float[]> camZones = new HashMap<Integer, float[]>();  // each is length cols*rows
HashMap<Integer, CameraDisplayCalibration> cameraCalibrations = new HashMap<Integer, CameraDisplayCalibration>();
String calibrationRoomName = "";
boolean hasRoomCalibration = false;

ArrayList<ZoneFlash> zoneFlashes = new ArrayList<ZoneFlash>();
ArrayList<GestureLogEntry> gestureLog = new ArrayList<GestureLogEntry>();
int maxGestureLog = 18;

boolean showVoiceGestures = true;
boolean showZoneGestures = true;
boolean showGlobalGestures = true;
boolean hostSendingEnabled = true;
boolean hostSensorsEnabled = true;

String lastGlobalGestureType = "";
float lastGlobalGestureStrength = 0.0;
int lastGlobalGestureFrame = -999;

void setup() {
  size(920, 720);
  frameRate(60);
  osc = new OscP5(this, 9000); // listen on port 9000
  hostControl = new NetAddress("127.0.0.1", 9001);
  loadRoomCalibration();

  textAlign(CENTER, CENTER);
  textSize(12);
}

void draw() {
  cleanupZoneFlashes();

  background(8);

  fill(255);
  textAlign(CENTER, CENTER);
  String title = "CrowdOrganDashboard - OSC monitor";
  if (hasRoomCalibration && calibrationRoomName.length() > 0) {
    title += " - " + calibrationRoomName;
  }
  text(title, width/2, 20);

  drawGlobalMeter();
  drawVoices();
  drawCameraGrids();
  drawGestureLog(width - 240, 120, 220, 260);
  drawFooter();
}

void drawGlobalMeter() {
  // A quick glance widget for how rowdy the room is overall.
  float gmWidth = 220;
  float gmHeight = 12;
  float gmX = width - gmWidth - 20;
  float gmY = 36;

  noFill();
  stroke(90);
  rect(gmX, gmY, gmWidth, gmHeight);

  noStroke();
  fill(0, 200, 255);
  rect(gmX, gmY, gmWidth * constrain(globalMotion, 0, 1), gmHeight);

  fill(255);
  textAlign(LEFT, CENTER);
  text("Global motion: " + nf(globalMotion, 1, 2), gmX, gmY - 12);

  if (showGlobalGestures && frameCount - lastGlobalGestureFrame < 240) {
    float age = (float)(frameCount - lastGlobalGestureFrame) / 240.0;
    int col = color(255, 0, 170, (1.0 - age) * 220);
    fill(col);
    textAlign(LEFT, CENTER);
    text("Last global gesture: " + lastGlobalGestureType + " (" + nf(lastGlobalGestureStrength, 1, 2) + ")", gmX, gmY + gmHeight + 14);
  }
}

void drawVoices() {
  textAlign(CENTER, CENTER);
  pushMatrix();
  translate(width/2 - 80, height/2);

  ArrayList<Integer> voiceIds = sortedKeys(voices);
  for (int i : voiceIds) {
    Voice v = voices.get(i);
    if (!v.active) continue;

    // Convert normalized coordinates to a simple stage map.
    float px = v.x * 320.0;
    float py = (1.0 - v.y) * 320.0;
    float radius = map(v.size, 0.0, 1.0, 12.0, 70.0);

    int baseCol = color(
      100 + 155 * v.energy,
      150,
      255
    );

    noStroke();
    fill(baseCol);
    ellipse(px, py, radius*2, radius*2);

    fill(255);
    text(i + " n:" + nf(v.note, 1, 1), px, py);

    if (showVoiceGestures && frameCount - v.lastGestureFrame < 180) {
      float age = (float)(frameCount - v.lastGestureFrame) / 180.0;
      float fade = constrain(1.0 - age, 0.0, 1.0);
      stroke(255, 140, 0, 200 * fade);
      strokeWeight(3);
      noFill();
      // The ring size scales with strength so you can eyeball intensity.
      float ring = radius*2 + 40 * v.lastGestureStrength;
      ellipse(px, py, ring, ring);
      strokeWeight(1);
      fill(255, 220);
      text(v.lastGestureType + " " + nf(v.lastGestureStrength, 1, 2), px, py - radius - 16);
    }
  }
  popMatrix();
}

void drawCameraGrids() {
  ArrayList<Integer> camIds = sortedKeys(camZones);
  if (camIds.isEmpty()) {
    return;
  }

  float gridWidth = width / (float)camIds.size();
  float gridHeight = 200;

  for (int camSlot = 0; camSlot < camIds.size(); camSlot++) {
    int camId = camIds.get(camSlot);
    float[] zones = camZones.get(camId);
    if (zones == null) continue;

    int cols = camCols.containsKey(camId) ? camCols.get(camId) : 0;
    int rows = camRows.containsKey(camId) ? camRows.get(camId) : 0;
    if (cols <= 0 || rows <= 0) continue;

    float cellW = gridWidth / cols;
    float cellH = gridHeight / rows;

    float baseX = camSlot * gridWidth;
    float baseY = height - gridHeight - 60;
    CameraDisplayCalibration calibration = cameraCalibrations.get(camId);

    fill(255);
    textAlign(LEFT, BOTTOM);
    text(cameraLabel(camId), baseX + 6, baseY - 6);

    ArrayList<ZoneFlash> pulses = new ArrayList<ZoneFlash>();
    ZoneFlash latestSweep = null;
    if (showZoneGestures) {
      for (ZoneFlash flash : zoneFlashes) {
        if (flash.camId != camId) continue;
        float age = frameCount - flash.startFrame;
        if (age > flash.lifespan) continue;
        if (flash.zoneIndex >= 0) {
          pulses.add(flash);
        } else if (latestSweep == null || flash.startFrame > latestSweep.startFrame) {
          latestSweep = flash;
        }
      }
    }

    int idx = 0;
    noStroke();
    for (int ry = 0; ry < rows; ry++) {
      for (int cx = 0; cx < cols; cx++) {
        if (idx >= zones.length) continue;
        float val = zones[idx];

        float x0 = baseX + cx * cellW;
        float y0 = baseY + ry * cellH;
        boolean ignored = isIgnoredZone(calibration, idx);

        int col = ignored ? color(32, 32, 38) : color(0, 130 + 125 * val, 255 * val);
        fill(col);
        rect(x0, y0, cellW, cellH);

        if (ignored) {
          drawIgnoredZone(x0, y0, cellW, cellH);
        }

        String zoneLabel = zoneLabel(calibration, idx);
        if (zoneLabel.length() > 0 && cellW >= 42 && cellH >= 24) {
          fill(ignored ? 150 : 230);
          textAlign(CENTER, CENTER);
          text(zoneLabel, x0 + cellW/2, y0 + cellH/2);
        }

        if (showZoneGestures) {
          for (ZoneFlash flash : pulses) {
            if (flash.zoneIndex == idx) {
              float age = frameCount - flash.startFrame;
              float fade = constrain(1.0 - age / (float)flash.lifespan, 0.0, 1.0);
              fill(255, 150, 0, 160 * fade);
              rect(x0, y0, cellW, cellH);
              fill(0);
              textAlign(CENTER, CENTER);
              text(nf(flash.strength, 1, 2), x0 + cellW/2, y0 + cellH/2);
            }
          }
        }

        idx++;
      }
    }

    if (showZoneGestures && latestSweep != null) {
      float age = frameCount - latestSweep.startFrame;
      float fade = constrain(1.0 - age / (float)latestSweep.lifespan, 0.0, 1.0);
      fill(255, 180, 0, 200 * fade);
      textAlign(LEFT, TOP);
      text("Sweep: " + latestSweep.type + " (" + nf(latestSweep.strength, 1, 2) + ")", baseX + 6, baseY + gridHeight + 6);
    }
  }
}

void drawIgnoredZone(float x, float y, float w, float h) {
  stroke(110);
  strokeWeight(1);
  line(x + 3, y + 3, x + w - 3, y + h - 3);
  line(x + w - 3, y + 3, x + 3, y + h - 3);
  noStroke();
}

void drawGestureLog(float x, float y, float w, float h) {
  if (gestureLog.isEmpty()) {
    return;
  }

  fill(0, 160);
  noStroke();
  rect(x, y, w, h);

  // The log doubles as a textual teaching aid: scope + label + type.
  textAlign(LEFT, TOP);
  fill(255);
  text("Gestures", x + 10, y + 8);

  float lineY = y + 28;
  float lineH = 16;
  for (int i = 0; i < gestureLog.size(); i++) {
    if (lineY > y + h - lineH) break;
    GestureLogEntry entry = gestureLog.get(i);
    float age = frameCount - entry.frame;
    float alpha = constrain(255 - age * 2, 80, 255);

    int scopeColor;
    if (entry.scope.equals("voice")) {
      scopeColor = color(0, 200, 255, alpha);
    } else if (entry.scope.equals("zone")) {
      scopeColor = color(255, 150, 0, alpha);
    } else if (entry.scope.equals("global")) {
      scopeColor = color(255, 0, 170, alpha);
    } else {
      scopeColor = color(200, alpha);
    }

    fill(scopeColor);
    String detail = entry.detail.length() > 0 ? (" " + entry.detail) : "";
    String label = "[" + entry.scope.charAt(0) + "] " + entry.label + " → " + entry.type + " " + nf(entry.strength, 1, 2) + detail;
    text(label, x + 10, lineY);
    lineY += lineH;
  }
}

void drawFooter() {
  textAlign(LEFT, BOTTOM);
  fill(180);
  text("toggles: [v] voice  [z] zone  [g] global  [r] reload  [m] mute  [s] sensors  [x] reset", 20, height - 18);

  String muted = "";
  if (!showVoiceGestures) muted += "voice muted  ";
  if (!showZoneGestures) muted += "zone muted  ";
  if (!showGlobalGestures) muted += "global muted";
  if (muted.length() > 0) {
    fill(255, 80, 80);
    text(muted, 20, height - 36);
  }
}

void cleanupZoneFlashes() {
  for (int i = zoneFlashes.size() - 1; i >= 0; i--) {
    ZoneFlash flash = zoneFlashes.get(i);
    if (frameCount - flash.startFrame > flash.lifespan) {
      zoneFlashes.remove(i);
    }
  }
}

void addZoneFlash(int camId, String type, float strength, int zoneIndex) {
  ZoneFlash flash = new ZoneFlash();
  flash.camId = camId;
  flash.type = type;
  flash.strength = strength;
  flash.zoneIndex = zoneIndex;
  flash.startFrame = frameCount;
  if (type.startsWith("pulse")) {
    flash.lifespan = 150;
  } else {
    flash.lifespan = 200;
  }
  zoneFlashes.add(flash);
}

void loadRoomCalibration() {
  ArrayList<String> candidates = new ArrayList<String>();
  candidates.add("room_calibration.json");
  String selectedHostCalibration = selectedHostCalibrationFile();
  if (selectedHostCalibration.length() > 0) {
    candidates.add("../of_app/bin/data/" + selectedHostCalibration);
  }
  candidates.add("../of_app/bin/data/room_calibration.json");

  JSONObject json = loadJSONObjectFromCandidates(candidates);
  if (json == null) {
    return;
  }

  calibrationRoomName = json.hasKey("room_name") ? json.getString("room_name", "") : "";
  if (!json.hasKey("cameras")) {
    return;
  }
  JSONArray cameras = json.getJSONArray("cameras");
  if (cameras == null) {
    return;
  }

  cameraCalibrations.clear();
  for (int i = 0; i < cameras.size(); i++) {
    JSONObject cameraJson = cameras.getJSONObject(i);
    if (cameraJson == null) continue;

    int camId = cameraJson.getInt("id", -1);
    if (camId < 0) continue;

    CameraDisplayCalibration calibration = new CameraDisplayCalibration();
    calibration.label = cameraJson.hasKey("label") ? cameraJson.getString("label", "") : "";

    if (cameraJson.hasKey("grid")) {
      JSONObject grid = cameraJson.getJSONObject("grid");
      calibration.cols = grid.getInt("cols", 0);
      calibration.rows = grid.getInt("rows", 0);
    }

    if (cameraJson.hasKey("ignored_zones")) {
      JSONArray ignored = cameraJson.getJSONArray("ignored_zones");
      for (int j = 0; j < ignored.size(); j++) {
        calibration.ignoredZones.add(Integer.valueOf(ignored.getInt(j)));
      }
    }

    if (cameraJson.hasKey("zone_labels")) {
      JSONObject labels = cameraJson.getJSONObject("zone_labels");
      for (Object rawKey : labels.keys()) {
        String key = (String)rawKey;
        try {
          calibration.zoneLabels.put(Integer.valueOf(parseInt(key)), labels.getString(key, ""));
        } catch (Exception e) {
          println("Skipping non-numeric zone label key: " + key);
        }
      }
    }

    cameraCalibrations.put(camId, calibration);
  }

  hasRoomCalibration = cameraCalibrations.size() > 0;
  println("Loaded room calibration for " + cameraCalibrations.size() + " camera(s)");
}

String selectedHostCalibrationFile() {
  try {
    JSONObject settings = loadJSONObject("../of_app/bin/data/gesture_settings.json");
    if (settings != null && settings.hasKey("room_calibration_file")) {
      String value = settings.getString("room_calibration_file", "");
      if (isSafeCalibrationPath(value)) {
        return value;
      }
    }
  } catch (Exception e) {
    // Dashboard-local calibration or default host calibration can still load.
  }
  return "";
}

boolean isSafeCalibrationPath(String value) {
  return value.length() > 5 &&
    !value.startsWith("/") &&
    value.indexOf("\\") < 0 &&
    value.indexOf("..") < 0 &&
    value.endsWith(".json");
}

JSONObject loadJSONObjectFromCandidates(ArrayList<String> candidates) {
  for (String candidate : candidates) {
    try {
      JSONObject json = loadJSONObject(candidate);
      if (json != null) {
        println("Loaded calibration: " + candidate);
        return json;
      }
    } catch (Exception e) {
      // Try the next candidate; the dashboard remains useful without labels.
    }
  }
  println("No room_calibration.json found for dashboard labels");
  return null;
}

String cameraLabel(int camId) {
  CameraDisplayCalibration calibration = cameraCalibrations.get(camId);
  if (calibration != null && calibration.label.length() > 0) {
    return "Cam " + camId + " - " + calibration.label;
  }
  return "Cam " + camId;
}

boolean isIgnoredZone(CameraDisplayCalibration calibration, int zoneIndex) {
  return calibration != null && calibration.ignoredZones.contains(Integer.valueOf(zoneIndex));
}

String zoneLabel(CameraDisplayCalibration calibration, int zoneIndex) {
  if (calibration == null) {
    return "";
  }
  String label = calibration.zoneLabels.get(Integer.valueOf(zoneIndex));
  return label == null ? "" : label;
}

ArrayList<Integer> sortedKeys(HashMap<Integer, ?> map) {
  ArrayList<Integer> keys = new ArrayList<Integer>(map.keySet());
  Collections.sort(keys);
  return keys;
}

Voice getVoice(int voiceId) {
  Voice v = voices.get(voiceId);
  if (v == null) {
    v = new Voice();
    voices.put(voiceId, v);
  }
  return v;
}

void pushGestureLog(String scope, String label, String type, float strength, String detail) {
  GestureLogEntry entry = new GestureLogEntry();
  entry.scope = scope;
  entry.label = label;
  entry.type = type;
  entry.strength = strength;
  entry.detail = detail;
  entry.frame = frameCount;
  gestureLog.add(0, entry);
  while (gestureLog.size() > maxGestureLog) {
    gestureLog.remove(gestureLog.size() - 1);
  }
}

void keyPressed() {
  if (key == 'v' || key == 'V') {
    showVoiceGestures = !showVoiceGestures;
  } else if (key == 'z' || key == 'Z') {
    showZoneGestures = !showZoneGestures;
  } else if (key == 'g' || key == 'G') {
    showGlobalGestures = !showGlobalGestures;
  } else if (key == 'r' || key == 'R') {
    sendHostControl("/room/config/reload");
  } else if (key == 'm' || key == 'M') {
    hostSendingEnabled = !hostSendingEnabled;
    sendHostControl("/room/config/sending", hostSendingEnabled ? 1 : 0);
  } else if (key == 's' || key == 'S') {
    hostSensorsEnabled = !hostSensorsEnabled;
    sendHostControl("/room/config/sensors", hostSensorsEnabled ? 1 : 0);
  } else if (key == 'x' || key == 'X') {
    sendHostControl("/room/global/reset");
  }
}

void sendHostControl(String address) {
  OscMessage msg = new OscMessage(address);
  osc.send(msg, hostControl);
}

void sendHostControl(String address, int value) {
  OscMessage msg = new OscMessage(address);
  msg.add(value);
  osc.send(msg, hostControl);
}

void oscEvent(OscMessage msg) {
  String addr = msg.addrPattern();

  if (addr.equals("/room/voice/active")) {
    int vid = msg.get(0).intValue();
    int activeFlag = msg.get(1).intValue();
    if (vid >= 0) {
      getVoice(vid).active = (activeFlag == 1);
    }

  } else if (addr.equals("/room/voice/state")) {
    int vid = msg.get(0).intValue();
    if (vid >= 0) {
      Voice v = getVoice(vid);
      v.active = true;
      v.x      = msg.get(1).floatValue();
      v.y      = msg.get(2).floatValue();
      v.z      = msg.get(3).floatValue();
      v.size   = msg.get(4).floatValue();
      v.motion = msg.get(5).floatValue();
      v.energy = msg.get(6).floatValue();
    }

  } else if (addr.equals("/room/voice/note")) {
    int vid = msg.get(0).intValue();
    if (vid >= 0) {
      Voice v = getVoice(vid);
      v.note     = msg.get(1).floatValue();
      v.velocity = msg.get(2).floatValue();
    }

  } else if (addr.equals("/room/global/motion")) {
    globalMotion = msg.get(0).floatValue();

  } else if (addr.equals("/room/camera/zones")) {
    int camId = msg.get(0).intValue();
    int cols  = msg.get(1).intValue();
    int rows  = msg.get(2).intValue();

    if (camId >= 0 && cols > 0 && rows > 0) {
      int numZones = cols * rows;
      if (msg.arguments().length >= 3 + numZones) {
        camCols.put(camId, cols);
        camRows.put(camId, rows);

        float[] zones = camZones.get(camId);
        if (zones == null || zones.length != numZones) {
          zones = new float[numZones];
          camZones.put(camId, zones);
        }

        for (int i = 0; i < numZones; i++) {
          zones[i] = msg.get(3 + i).floatValue();
        }
      }
    }

  } else if (addr.equals("/room/gesture/voice")) {
    int vid = msg.get(0).intValue();
    String type = msg.get(1).stringValue();
    float strength = msg.get(2).floatValue();
    float extra = msg.get(3).floatValue();
    handleVoiceGesture(vid, type, strength, extra);

  } else if (addr.equals("/room/gesture/zone")) {
    int camId = msg.get(0).intValue();
    String type = msg.get(1).stringValue();
    float strength = msg.get(2).floatValue();
    int zoneIndex = (msg.arguments().length >= 4) ? msg.get(3).intValue() : -1;
    handleZoneGesture(camId, type, strength, zoneIndex);

  } else if (addr.equals("/room/gesture/global")) {
    String type = msg.get(0).stringValue();
    float strength = msg.get(1).floatValue();
    handleGlobalGesture(type, strength);
  }
}

void handleVoiceGesture(int voiceId, String type, float strength, float extra) {
  if (voiceId >= 0) {
    Voice v = getVoice(voiceId);
    v.lastGestureType = type;
    v.lastGestureStrength = strength;
    v.lastGestureFrame = frameCount;
  }

  // Extra carries additional context such as y-position or hold duration.
  String detail = "";
  if (type.equals("raise") || type.equals("lower")) {
    detail = "@y " + nf(extra, 1, 2);
  } else if (type.equals("hold")) {
    detail = "len " + nf(extra, 1, 2);
  }
  pushGestureLog("voice", "voice " + voiceId, type, strength, detail);
}

void handleZoneGesture(int camId, String type, float strength, int zoneIndex) {
  addZoneFlash(camId, type, strength, zoneIndex);
  String detail = (zoneIndex >= 0) ? ("zone " + zoneIndex) : "";
  pushGestureLog("zone", "cam " + camId, type, strength, detail);
}

void handleGlobalGesture(String type, float strength) {
  lastGlobalGestureType = type;
  lastGlobalGestureStrength = strength;
  lastGlobalGestureFrame = frameCount;
  pushGestureLog("global", "room", type, strength, "");
}
