// PC Monitor — настольный корпус для ES3C28P (2.8" ESP32-S3), экран под наклоном.
// Две детали: part="shell" — рамка с окном экрана, скруглённым лицом и ретро-канавкой (печать лицом вниз),
//              part="back"  — спинка-клин: крышка + опора + полозок под низом, держит экран под наклоном tilt
//                             (выводится стоя на основании — так и печатать).
// part="asm" — сборка как стоит на столе (для просмотра). USB-C оказывается сверху, кабель уходит назад.
// Размеры сняты с нашего прошлого корпуса ES3C28P_retro_korpus_v4 (проверен на плате):
// стойки 77.3×42, окно 61.4×48 (смещено на +2.7 мм по длине), лицо→плата 7.5 мм,
// USB-C — вырез 21×7.8 на ДЛИННОЙ стороне (+7.1 мм от центра), на торце — вырез 12.4×7.7.

part = "asm";            // "shell" | "back" | "asm"
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
fil = 3;                 // скругление переднего ребра
cap_t = 2.4;
board_back = lip + glass_t + pcb_t;      // тыльная сторона платы от лица
Dt = depth + cap_t;                      // толщина корпуса с крышкой
W = out_w;
lug_x = [-25, 28];
Lb = 40;                                 // длина опоры за корпусом
h_ = [-sin(tilt), cos(tilt)];            // «назад по столу» в координатах (y,z) корпуса
V_ = [-cos(tilt), -sin(tilt)];           // «вверх»

// -------------------------------------------------------------- рамка
module shell_body() {
  hull() {
    for (i = [0:6]) let(a = 90 * i / 6, ins = fil - fil * sin(a))
      translate([0, 0, fil - fil * cos(a)]) rbox(out_l - 2 * ins, out_w - 2 * ins, 0.01, max(r_out - ins, 0.6));
    translate([0, 0, fil]) rbox(out_l, out_w, depth - fil, r_out);
  }
}
module shell() {
  difference() {
    shell_body();
    translate([0, 0, lip + glass_t - 0.01]) rbox(in_l, in_w, depth, 1);
    translate([glass_shift, 0, lip]) rbox(glass_l + 2 * fit, glass_w + 2 * fit, glass_t + 0.02, 1.2);
    // окно со скосом
    translate([glass_shift + aa_shift, 0, -0.01])
      hull() {
        rbox(aa_l + 1.0 + 2 * lip, aa_w + 1.0 + 2 * lip, 0.01, 2);
        translate([0, 0, lip]) rbox(aa_l + 1.0, aa_w + 1.0, 0.03, 1.5);
      }
    // ретро-канавка по периметру
    translate([0, 0, depth - 2.6]) linear_extrude(1.2) difference() {
      offset(r_out + 2) square([out_l - 2 * r_out, out_w - 2 * r_out], center = true);
      offset(r_out - 0.6) square([out_l - 2 * r_out, out_w - 2 * r_out], center = true);
    }
    // USB-C / microSD — длинная сторона (при установке — сверху)
    translate([usb_off, -out_w / 2, usb_d]) rotate([90, 0, 0])
      linear_extrude(wall * 3, center = true) offset(1.5) square([usb_len - 3, usb_h - 3], center = true);
    // 2 винта M3 крепят спинку (сверху, рядом с USB)
    for (x = lug_x) translate([x, -out_w / 2, depth - 3]) rotate([90, 0, 0]) cylinder(d = 3.4, h = wall * 3, center = true);
    // вырез на коротком торце
    translate([-out_l / 2, 0, end_d]) rotate([0, 90, 0])
      linear_extrude(wall * 3, center = true) offset(1.5) square([end_h - 3, end_w - 3], center = true);
  }
}

// -------------------------------------------------------------- спинка-клин
P_ = [W / 2, Dt];
C_ = P_ + Lb * h_;
module wedge_hull() {
  r = 5;
  hull() {
    polygon([[-W / 2, depth], [W / 2, depth], P_, [-W / 2 + r, Dt]]);
    translate([-W / 2 + r, Dt - r]) circle(r);
    translate(C_ - 2 * r * h_ - r * V_) circle(r);
  }
}
module wedge2d() {
  wedge_hull();
  // полозок под нижней гранью рамки
  polygon([[W / 2, depth], P_, [W / 2 + (Dt - 1.5) * tan(tilt), 1.5], [W / 2, 1.5]]);
}
module cavity2d() {
  module c() intersection() { offset(delta = -2.4) wedge_hull(); translate([-100, Dt]) square(200); }
  hull() { c(); translate(30 * -V_) c(); }
}
module xprof() { rotate([90, 0, 90]) linear_extrude(out_l, center = true) children(); }
module back() {
  difference() {
    union() {
      intersection() {
        xprof() wedge2d();
        // скругление задних углов по краям
        rotate([90, 0, 0]) linear_extrude(200, center = true)
          translate([0, 60]) offset(r_out) square([out_l - 2 * r_out, 200], center = true);
      }
      // ободок внутрь рамки
      translate([0, 0, depth - 2]) difference() {
        rbox(in_l - 0.3, in_w - 0.3, 2.01, 1);
        translate([0, 0, -0.1]) rbox(in_l - 3.5, in_w - 3.5, 2.3, 0.5);
      }
      // ушки под 2 винта через верхнюю стенку рамки
      for (x = lug_x) translate([x, -in_w / 2 + 0.15 + 2.5, depth - 3]) cube([8, 5, 6], center = true);
      // стойки к отверстиям платы
      for (x = [-1, 1], y = [-1, 1]) translate([x * hole_dx / 2, y * hole_dy / 2, board_back + 0.2])
        cylinder(d = 6.2, h = depth - board_back);
    }
    // полость клина (открыта к столу)
    rotate([90, 0, 90]) linear_extrude(out_l - 4.8, center = true) cavity2d();
    // плата крепится к стойкам саморезами со стороны экрана (отверстия платы вне стекла)
    for (x = [-1, 1], y = [-1, 1]) translate([x * hole_dx / 2, y * hole_dy / 2, board_back - 1])
      cylinder(d = screw_d, h = depth - board_back);
    for (x = lug_x) translate([x, -in_w / 2, depth - 3]) rotate([90, 0, 0]) cylinder(d = 2.6, h = 14, center = true);
    // вентиляция в крышке (воздух уходит в полость и вниз)
    for (i = [-3:3]) translate([i * 8, -4, depth - 3]) rbox(3, out_w * 0.4, cap_t + 4, 1.4);
  }
}
// перевод из координат корпуса в «стоит на столе»
module world() {
  multmatrix([[1, 0, 0, 0], [0, -sin(tilt), cos(tilt), 0], [0, -cos(tilt), -sin(tilt), cos(tilt) * W / 2 + sin(tilt) * Dt], [0, 0, 0, 1]])
    children();
}

if (part == "shell") shell();
else if (part == "back") world() back();
else world() { color("#3c6aa8") shell(); color("#e9e4d8") back(); }
