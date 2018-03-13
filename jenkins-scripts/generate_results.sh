#!/bin/sh

set -x

JENKINS_HOME=/home/rahmanmd/.jenkins
RESULTS_DIR=$JENKINS_HOME/results
# Static location now. May need to change later
GNUPLOT_BINARY=/home/rahmanmd/gnuplot-install/bin

if [ -z "$(ls -A $RESULTS_DIR)" ] 
then
    echo "Results directory is empty. Exiting."
    exit 0
fi

cd $RESULTS_DIR
rm -rf *.pdf

for file in bw-*
do
    export plot_name=$file
cat > "$file".plot << "EOF"
set terminal postscript enhanced "Helvetica" 22
set pointsize 2
unset key
set logscale x 2
set logscale y 10
set term postscript color
set xlabel 'Message Size (B)'
set ylabel 'Uni-directional Bandwidth (MBps)'
set xtics(1,4,16,64,256,"1K" 1024,"4K" 4096,"16K" 16384, "64K" 65536, "256K" 262144, "1M" 1048576, "4M" 4194304)
set grid
set style line 1 lt 1 lw 6 pt 4 lc rgb "red"
set style line 2 lt 1 lw 6 pt 7 lc rgb "blue"
set style line 3 lt 1 lw 6 pt 5 lc rgb "black"

name=system("echo $plot_name")

set title sprintf('%s', name)
set output sprintf('%s.eps', name)

plot name with lp ls 1

EOF
    $GNUPLOT_BINARY/gnuplot "$file".plot
    ps2pdf "$file".eps
    rm "$file".eps
    rm "$file".plot
done

cd $RESULTS_DIR
cat > results.html << EOF
<html lang="en">
<head>
  <title>Performance tests</title>
  <link rel="stylesheet" href="http://code.jquery.com/ui/1.12.1/themes/base/jquery-ui.css">
  <style>
#loader {
    border: 16px solid #f3f3f3; /* Light grey */
    border-top: 16px solid #3498db; /* Blue */
    border-radius: 50%;
    width: 120px;
    height: 120px;
    animation: spin 2s linear infinite;
    position: absolute;
    top:0;
    bottom: 0;
    left: 0;
    right: 0;
    margin: auto;
}
@keyframes spin {
    0% { transform: rotate(0deg); }
    100% { transform: rotate(360deg); }
}
.perf_info_div {
    margin-left: 275px;
}
.perf_td {
     vertical-align: 0px;	
}
.outfile { 
     font-family: Consolas,Menlo,Monaco,Lucida Console,Liberation Mono,DejaVu Sans Mono,Bitstream Vera Sans Mono,Courier New,monospace,sans-serif;
     background-color: #efefef;
}
  </style>
  <script type="text/javascript" src="https://www.gstatic.com/charts/loader.js"></script>
  <script src="https://code.jquery.com/jquery-1.12.4.js"></script>
  <script src="http://ajax.googleapis.com/ajax/libs/jqueryui/1.10.3/jquery-ui.js"></script>
</head>
<body>
      <div class="panel-body">
        <h2 class="text-center caption"></h2>
        <div class="row">
          <div class="col-sm-6">
            <h4 class="text-center caption">Graphs</h4>
		<img class="img-rounded" data-src="holder.js/30x20" src="bw-shmem_bw_put_perf-gcc.png" alt="SHMEM Put BW" />
          </div>
        </div>
      </div>
</body>
</html>
EOF
