
$fn=50;

module dps() {
  color("green") union() {
    translate([30,35/2,22/2]) cube ([60,35,22],center=true);
    translate([60,4,4]) cube ([10,9.2,10.5]);
  }
}

module box() {
  difference() {
    linear_extrude (height=28) offset (r=5) square ([100,100], center=true);
    translate ([0,0,30/2+2]) cube ([95,95,30], center=true);
    translate ([0,0,4/2+24+0.05]) cube([100,100,4.1], center=true);
  }
}

difference() {
  box();
  translate([-8,-35/2,1]) dps();
}
%translate ([0,0,4/2+24]) cube([100,100,4], center=true);
