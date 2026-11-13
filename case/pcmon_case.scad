// PC Monitor — настольный корпус для ES3C28P (2.8" ESP32-S3), экран под наклоном.
// Три детали: part="shell" — рамка-коробка с окном экрана (печать лицом вниз),
//              part="back"  — задняя крышка со стойками под винты M3 (печать плашмя),
//              part="stand" — подставка-ложемент с наклоном tilt (печать на боку не нужна).
// Размеры сняты с нашего прошлого корпуса ES3C28P_retro_korpus_v4 (проверен на плате):
// стойки 77.3×42, окно 61.4×48 (смещено на +2.7 мм по длине), лицо→плата 7.5 мм,
// USB-C — вырез 21×7.8 на ДЛИННОЙ стороне (+7.1 мм от центра), на торце — вырез 12.4×7.7.

part = "all";            // "shell" | "back" | "stand" | "all" (для просмотра)
tilt = 20;               // наклон экрана назад от вертикали, градусов (15–30)

// ---- модуль
pcb_l = 86;  pcb_w = 50;  pcb_t = 1.6;
glass_l = 69.2; glass_w = 50; glass_t = 5.9;    // стекло+клей+тач над платой
aa_l = 61.4 - 1.0; aa_w = 48 - 1.0;                          // видимая область
glass_shift = 2.7;         // смещение стекла от центра платы по длине (мм, + к USB)
aa_shift = 0;            // смещение видимой области от центра стекла
hole_dx = 77.3; hole_dy = 42; hole_d = 3.2;
back_parts = 6.0;        // высота деталей на обороте платы
// вырезы (глубина считается от лица корпуса)
usb_off = 7.1;  usb_len = 21;  usb_h = 7.8;  usb_d = 10.9;   // длинная сторона (y-)
end_w = 12.4;   end_h = 7.7;   end_d = 10.0;                  // короткий торец (x-)

// ---- корпус
wall = 2.2;              // стенки
lip = 1.6;               // рамка над стеклом
fit = 0.35;              // зазор вокруг модуля
r_out = 4;               // скругление углов
gap_back = 3;            // воздух за деталями платы (провод USB, динамик)
screw_d = 2.6;           // отверстие под саморез M3 в стойке крышки (или 4.0 под вставку)

in_l = pcb_l + 2 * fit;
in_w = pcb_w + 2 * fit;
depth = lip + glass_t + pcb_t + back_parts + gap_back;   // внутренняя глубина
out_l = in_l + 2 * wall;
out_w = in_w + 2 * wall;
$fn = 48;

module rbox(l, w, h, r) {
  linear_extrude(h) offset(r) square([l - 2 * r, w - 2 * r], center = true);
}

// -------------------------------------------------------------- рамка-коробка
// Лицо рамки — на z=0 (печатается лицом на стол), открыта сзади.
module shell() {
  difference() {
    rbox(out_l, out_w, depth, r_out);
    // полость под модуль
    translate([0, 0, lip + glass_t - 0.01]) rbox(in_l, in_w, depth, 1);
    // гнездо стекла
    translate([glass_shift, 0, lip]) rbox(glass_l + 2 * fit, glass_w + 2 * fit, glass_t + 0.02, 1.2);
    // окно экрана со скосом
    translate([glass_shift + aa_shift, 0, -0.01])
      hull() {
        rbox(aa_l + 1.0 + 2 * lip, aa_w + 1.0 + 2 * lip, 0.01, 2);
        translate([0, 0, lip]) rbox(aa_l + 1.0, aa_w + 1.0, 0.03, 1.5);
      }
    // USB-C / microSD — на длинной стороне, как в прошлом корпусе
    translate([usb_off, -out_w / 2, usb_d]) rotate([90, 0, 0])
      linear_extrude(wall * 3, center = true) offset(1.5) square([usb_len - 3, usb_h - 3], center = true);
    // вырез на коротком торце
    translate([-out_l / 2, 0, end_d]) rotate([0, 90, 0])
      linear_extrude(wall * 3, center = true) offset(1.5) square([end_h - 3, end_w - 3], center = true);
  }
  // упоры за платой: прижимают её к стеклу через крышку (стойки под 4 отверстия платы)
}

// -------------------------------------------------------------- крышка с упором
// Крышка входит внутрь рамки на 2 мм и держит плату стойками через её отверстия.
cap_t = 2.4;
module back() {
  stand_h = gap_back + back_parts;          // от крышки до платы
  difference() {
    union() {
      rbox(out_l, out_w, cap_t, r_out);
      // ободок, заходящий в рамку
      translate([0, 0, cap_t]) difference() {
        rbox(in_l - 0.3, in_w - 0.3, 2, 1);
        translate([0, 0, -0.1]) rbox(in_l - 0.3 - 3.2, in_w - 0.3 - 3.2, 2.3, 0.5);
      }
      // стойки к отверстиям платы
      for (x = [-1, 1], y = [-1, 1]) translate([x * hole_dx / 2, y * hole_dy / 2, cap_t])
        cylinder(d = 6.2, h = stand_h - 0.2);
    }
    for (x = [-1, 1], y = [-1, 1]) translate([x * hole_dx / 2, y * hole_dy / 2, -0.1])
      cylinder(d = screw_d, h = cap_t + stand_h + 1);
    // зенковка под головки снаружи
    for (x = [-1, 1], y = [-1, 1]) translate([x * hole_dx / 2, y * hole_dy / 2, -0.1])
      cylinder(d1 = 6.2, d2 = 3.2, h = 1.6);
    // вентиляция
    for (i = [-3:3]) translate([i * 8, 0, -0.1]) rbox(3, out_w * 0.45, cap_t + 0.2, 1.4);
  }
}

// -------------------------------------------------------------- подставка
// Ложемент: корпус ставится на наклонное «сиденье», упирается спиной в наклонную спинку,
// снизу его держит бортик. Угол наклона экрана назад от вертикали — tilt.
D = depth + cap_t;                     // толщина собранного корпуса
st_w = out_l * 0.62;                   // ширина подставки; ставьте корпус USB-C вверх (поворот экрана — в компаньоне)
module stand() {
  base = 4; lipH = 4; rest = 34; rt = 4;
  sy = base + D * sin(tilt);           // высота переднего края сиденья
  B = [3 + D * cos(tilt), base];       // задняя кромка корпуса на сиденье
  P5 = B + rest * [sin(tilt), cos(tilt)];
  P6 = P5 + rt * [cos(tilt), -sin(tilt)];
  pts = [[-2, 0], [-2, sy + lipH], [3, sy + lipH], [3, sy], B, P5, P6, [P6[0] + 6, 0]];
  rotate([90, 0, 90]) translate([0, 0, -st_w / 2]) linear_extrude(st_w) polygon(pts);
}

// -------------------------------------------------------------- вывод
if (part == "shell") shell();
else if (part == "back") back();
else if (part == "stand") stand();
else {
  shell();
  translate([0, 0, depth + 15]) back();
  translate([0, out_w + 30, 0]) stand();
}
