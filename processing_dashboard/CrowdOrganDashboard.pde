import oscP5.*;
import netP5.*;

OscP5 osc;

int NUM_VOICES = 8;
int CAMS = 2;

class Voice {
  boolean active = false;
  float x, y, z;
  float size;
  float motion;
  float energy;
  float note;
  float velocity;
}

Voice[] voices = new Voice[NUM_VOICES];

float globalMotion = 0.0;

int[] camCols = new int[CAMS];
int[] camRows = new int[CAMS];
float[][] camZones = new float[CAMS][];  // each is length cols*rows

void setup() {
  size(900, 700);
  frameRate(60);
  osc = new OscP5(this, 9000); // listen on port 9000

  for (int i = 0; i < NUM_VOICES; i++) {
    voices[i] = new Voice();
  }

  textAlign(CENTER, CENTER);
  textSize(12);
}

void draw() {
  background(0);

  fill(255);
  textAlign(CENTER, CENTER);
  text("CrowdOrganDashboard - OSC monitor", width/2, 20);

  // Global motion meter
  float gmWidth = 200;
  float gmHeight = 10;
  float gmX = width - gmWidth - 20;
  float gmY = 20;

  noFill();
  stroke(150);
  rect(gmX, gmY, gmWidth, gmHeight);

  noStroke();
  fill(0, 200, 255);
  rect(gmX, gmY, gmWidth * constrain(globalMotion, 0, 1), gmHeight);

  fill(255);
  textAlign(LEFT, CENTER);
  text("Global motion: " + nf(globalMotion, 1, 2), gmX, gmY - 10);

  // Voices (center)
  textAlign(CENTER, CENTER);
  pushMatrix();
  translate(width/2, height/2);

  for (int i = 0; i < NUM_VOICES; i++) {
    Voice v = voices[i];
    if (!v.active) continue;

    float px = v.x * 300.0;
    float py = (1.0 - v.y) * 300.0;
    float radius = map(v.size, 0.0, 1.0, 10.0, 60.0);

    int c = color(
      100 + 155 * v.energy,
      150,
      255
    );

    noStroke();
    fill(c);
    ellipse(px, py, radius*2, radius*2);

    fill(255);
    text(i + " n:" + nf(v.note, 1, 1), px, py);
  }
  popMatrix();

  // Draw camera grids at the bottom
  float gridWidth = width / (float)CAMS;
  float gridHeight = 180;

  for (int camId = 0; camId < CAMS; camId++) {
    if (camZones[camId] == null) continue;

    int cols = camCols[camId];
    int rows = camRows[camId];
    if (cols <= 0 || rows <= 0) continue;

    float cellW = gridWidth / cols;
    float cellH = gridHeight / rows;

    float baseX = camId * gridWidth;
    float baseY = height - gridHeight - 40;

    // Label
    fill(255);
    textAlign(LEFT, BOTTOM);
    text("Cam " + camId, baseX + 4, baseY - 4);

    // Cells
    int idx = 0;
    noStroke();
    for (int ry = 0; ry < rows; ry++) {
      for (int cx = 0; cx < cols; cx++) {
        float val = camZones[camId][idx];
        idx++;

        float x0 = baseX + cx * cellW;
        float y0 = baseY + ry * cellH;

        int col = color(0, 150 + 105 * val, 255 * val);
        fill(col);
        rect(x0, y0, cellW, cellH);
      }
    }
  }
}

void oscEvent(OscMessage msg) {
  String addr = msg.addrPattern();

  if (addr.equals("/room/voice/active")) {
    int vid = msg.get(0).intValue();
    int activeFlag = msg.get(1).intValue();
    if (vid >= 0 && vid < NUM_VOICES) {
      voices[vid].active = (activeFlag == 1);
    }

  } else if (addr.equals("/room/voice/state")) {
    int vid = msg.get(0).intValue();
    if (vid >= 0 && vid < NUM_VOICES) {
      Voice v = voices[vid];
      v.x      = msg.get(1).floatValue();
      v.y      = msg.get(2).floatValue();
      v.z      = msg.get(3).floatValue();
      v.size   = msg.get(4).floatValue();
      v.motion = msg.get(5).floatValue();
      v.energy = msg.get(6).floatValue();
    }

  } else if (addr.equals("/room/voice/note")) {
    int vid = msg.get(0).intValue();
    if (vid >= 0 && vid < NUM_VOICES) {
      Voice v = voices[vid];
      v.note     = msg.get(1).floatValue();
      v.velocity = msg.get(2).floatValue();
    }

  } else if (addr.equals("/room/global/motion")) {
    globalMotion = msg.get(0).floatValue();

  } else if (addr.equals("/room/camera/zones")) {
    int camId = msg.get(0).intValue();
    int cols  = msg.get(1).intValue();
    int rows  = msg.get(2).intValue();

    if (camId >= 0 && camId < CAMS && cols > 0 && rows > 0) {
      int numZones = cols * rows;
      if (msg.arguments().length >= 3 + numZones) {
        camCols[camId] = cols;
        camRows[camId] = rows;

        if (camZones[camId] == null || camZones[camId].length != numZones) {
          camZones[camId] = new float[numZones];
        }

        for (int i = 0; i < numZones; i++) {
          camZones[camId][i] = msg.get(3 + i).floatValue();
        }
      }
    }
  }
}
