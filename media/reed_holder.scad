use <scadlib/scadlib.scad>
include <scadlib/truefalse.scad>

minth = 2;
screwd = 3.5;
du = .01;
fn = 16;
more = 2;
cable_d = 4;

module cube_magnet_holder(l=5.15, width=3, offset=2, slide=8)
{
	difference()
	{
		union()
		{
			hull()
			{
				//%translate([0,-more,0]) prism(fn,[width*l+4*minth+2*(screwd+more), l+minth+2*more, minth], center=tff);
				translate([-width*l/2-2*minth,-more,0]) prism(fn,[l+minth+2*more, 2*(screwd+more)+slide, minth], center=tff);
				translate([ width*l/2+2*minth,-more,0]) prism(fn,[l+minth+2*more, 2*(screwd+more)+slide, minth], center=tff);
				translate([0,-offset,0]) prism(0,[width*l, l+minth+offset, minth], center=tff);
			}
			hull()
			{
				translate([0,-offset,0]) prism(0,[width*l-2*more, l+minth+offset+slide, minth], center=tff);
				translate([0,-offset,0]) prism(0,[l+2*minth, l+minth, l+2*minth], center=tff);
			}
		}
		union()
		{
			translate([0,-du-offset,minth]) prism(0,[l,l,l],center=tff);
			hull()
			{
				translate([-(width*l/2+minth+screwd/2),l/2+minth/2+slide,-du]) cylinder(d1=screwd,d2=screwd+2*minth,h=minth+2*du);
				translate([-(width*l/2+minth+screwd/2),l/2+minth/2,-du]) cylinder(d1=screwd,d2=screwd+2*minth,h=minth+2*du);
			}
			hull()
			{
				translate([ (width*l/2+minth+screwd/2),l/2+minth/2+slide,-du]) cylinder(d1=screwd,d2=screwd+2*minth,h=minth+2*du);
				translate([ (width*l/2+minth+screwd/2),l/2+minth/2,-du]) cylinder(d1=screwd,d2=screwd+2*minth,h=minth+2*du);
			}
		}
	}
}

/*
 * almost the same as cube_magnet_holder, but with more parameters (and has fixed position (no slides)
 */
module reed_holder(l=75, d=5.15, width=1.3, offset=5, wire_d=3, exit_left=true)
{
	difference()
	{
		union()
		{
			hull()
			{
				translate([0,-more,0]) prism(fn,[width*l+4*minth+2*(screwd+more), d+minth+2*more, minth], center=tff);
				translate([0,-offset,0]) prism(0,[width*l, d+minth+offset, minth], center=tff);
			}
			hull()
			{
				translate([0,-offset,0]) prism(0,[width*l-2*more, d+minth+offset, minth], center=tff);
				translate([0,-offset,0]) prism(0,[l+2*minth, d+minth, d+2*minth], center=tff);
			}
		}
		union()
		{
			translate([0,-du-offset,minth]) prism(0,[l,d,d],center=tff);
			translate([-(width*l/2+minth+screwd/2),d/2+minth/2,-du]) cylinder(d1=screwd,d2=screwd+2*minth,h=minth+2*du);
			translate([ (width*l/2+minth+screwd/2),d/2+minth/2,-du]) cylinder(d1=screwd,d2=screwd+2*minth,h=minth+2*du);
			// wire exit
			translate([(exit_left ? -l/2:l/2),-du-offset+d,minth+wire_d/2])
			rotate([0,(exit_left ? 90:-90),0]) cylinder(d=wire_d,h=l*width,$fn=8);
		}
	}
	// print supports (remove after)
	n = 2; // how many (per side! ATM always one in the middle)
	for (i=[-n:1:n])
	translate([(i==0?0:l/(2*(n+1))*i),-offset,minth]) prism(0,[1,.7,d],center=tff);
}

module cable_holder()
{
   width = 10;
   l = 8;
   intersection()
   {
   	prism(0,[3*width,l+2*width,cable_d+minth]);
   	difference()
   	{
   		union()
		{
			prism(fn,[width, width+l, minth]);
			translate([0,-l/2,0]) rotate([90,0,90]) prism(fn,[cable_d+2*minth,2*cable_d+2*minth,width],center=ttt);
		}
		union()
		{
			translate([ 0,l/2,-du]) cylinder(d1=screwd,d2=screwd+2*minth,h=minth+2*du);
			translate([0,-l/2,0]) rotate([90,0,90]) prism(fn,[cable_d,2*cable_d,2*width],center=ttt);
		}
   	}
   }
}

//translate([0,-10,0]) rotate([0,0,180])
*cube_magnet_holder();
*reed_holder(exit_left=false);

cable_holder();
