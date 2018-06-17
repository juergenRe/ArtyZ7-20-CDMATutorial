# Run this script to create the Vivado project files NEXT TO THIS script
# If ::create_path global variable is set, the project is created under that path instead of the working dir

# only sample!

# Project specific settings. These must be updated for each project.
set proj_name "test"

if {[info exists ::create_path]} {
	set dest_dir $::create_path
} else {
	set dest_dir [file normalize [file dirname [info script]]]
}
puts "INFO: Creating new project in $dest_dir"
cd $dest_dir



set part "xc7z020clg400-1"
set brd_part "digilentinc.com:arty-z7-20:part0:1.0"

# Set the reference directory for source file relative paths (by default the value is script directory path)
set origin_dir ".."

# Set the directory path for the original project from where this script was exported
set orig_proj_dir "[file normalize "$origin_dir/proj"]"

set src_dir $origin_dir/src
set repo_dir $origin_dir/repo

# # Set the board repo
# # Uncomment if distributing board files with project in the "repo/board_files" folder.
# # This is currently untested. It intends to also keep any existing board repo paths, since this is a global Vivado setting (not project specific.
# # Ideally, if the project is closed, and then a new project is created (without closing Vivado), this should still be able to see a board repo specified in init.tcl.
#set_param board.repoPaths "[file normalize "$repo_dir/board_files"]"

# 
# 
# puts "INFO: Block design ready: $design_name.bd"