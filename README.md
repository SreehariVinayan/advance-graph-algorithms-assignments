# PECST595 – Advanced Graph Algorithms

Research assignments for **PECST595 – Advanced Graph Algorithms** at the **College of Engineering Trivandrum (CET)**.

This repository contains implementations and experimental evaluations of graph algorithms for network optimization. Each assignment includes source code, benchmark utilities, datasets or dataset acquisition instructions, results, visualizations, and a report.

## Assignments

### Assignment 1: Comparative Analysis of Maximum Flow Algorithms

A comparative performance study of algorithms for maximum-flow problems motivated by network bandwidth allocation.

**Algorithms**
- Dinic's Algorithm
- Push–Relabel Algorithm

**Evaluation**
- Maximum flow value and correctness
- Execution time
- Memory usage
- Scalability across network sizes and structures
- Algorithm-specific operation counts

### Assignment 2: Comparative Analysis of Minimum-Cost Flow Algorithms

A comparative performance study of algorithms for minimum-cost flow problems using road-network instances.

**Algorithms**
- Cycle-Canceling Algorithm
- Capacity Scaling Algorithm

**Evaluation**
- Feasibility and total flow/cost
- Execution time
- Memory usage
- Scalability across network sizes and structures
- Algorithm-specific operation counts

Both implementations are evaluated on matching input instances. Matching objective values are useful for cross-checking, but correctness should also be assessed through feasibility and capacity/conservation constraints.


## Build and Run

The algorithms are written in C and can be compiled with a C compiler such as GCC. Build commands depend on the source-file organization and any local build scripts.

For a simple single-file program:

```bash
gcc -O2 -std=c11 -Wall -Wextra src/algorithm.c -o algorithm
./algorithm
```

Replace `src/algorithm.c` and the executable name with the relevant source and target. If an assignment provides a `Makefile` or build script, use that instead.

Python visualization scripts may require Python 3, pandas, and Matplotlib:

```bash
python3 -m pip install pandas matplotlib
python3 path/to/plot_results.py
```

Replace the example script path with the visualization script in the relevant assignment. Refer to each assignment's source and report for its expected input format and execution options.

## Datasets and Licensing

This repository references network topology and minimum-cost-flow benchmark data from external sources. **Dataset files are not included by default because redistribution permissions have not yet been verified.** 


## Results

Assignment results and visualizations are organized under each assignment's `results/` and `plots/` directories. CSV files are used for structured benchmark data where applicable. Reports describe the experimental setup, findings, limitations, and interpretation.

## References

### Maximum-Flow Topologies

1. Topology Zoo / TopoHub, “backbone/africa.” [https://www.topohub.org/?topology=backbone/africa](https://www.topohub.org/?topology=backbone/africa)
2. M. Grey, M. Theil, M. Rossberg, and G. Schaefer, “Towards a model for global-scale backbone networks,” *Proc. IEEE ICC*, London, U.K., 2015, pp. 6086–6091, doi: [10.1109/ICC.2015.7249292](https://doi.org/10.1109/ICC.2015.7249292).
3. CAIDA, “Internet Topology Data Kit.” [https://www.caida.org/catalog/datasets/internet-topology-data-kit/](https://www.caida.org/catalog/datasets/internet-topology-data-kit/)

### Minimum-Cost-Flow Instances

4. P. Kovács (maintainer), “Minimum cost flow problem instances – road networks,” LEMON benchmark data. [http://lime.cs.elte.hu/~kpeter/data/mcf/road/](http://lime.cs.elte.hu/~kpeter/data/mcf/road/)
5. LEMON Graph Library, “Min cost flow data (MinCostFlowData),” project wiki. [https://lemon.cs.elte.hu/trac/lemon/wiki/MinCostFlowData](https://lemon.cs.elte.hu/trac/lemon/wiki/MinCostFlowData)

## Course Information

- **Course:** PECST595 – Advanced Graph Algorithms
- **Institution:** College of Engineering Trivandrum (CET)
- **Work:** Research assignments

## Future Work

Additional assignments, algorithms, datasets, and comparative evaluations may be added as the course progresses.
