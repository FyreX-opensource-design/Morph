#needed for java apps to work properly
export _JAVA_AWT_WM_NONREPARENTING=1

#start waybar
waybar -c ./config/waybar-stackcomp.jsonc -s ./config/waybar-stackcomp.css &

#start xwayland-satellite
xwayland-satellite &