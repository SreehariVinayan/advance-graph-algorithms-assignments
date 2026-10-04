
import os
import argparse
import pandas as pd
import matplotlib.pyplot as plt

# Configuration
PLOT_DIR = "plots"
os.makedirs(PLOT_DIR, exist_ok=True)

COLORS = {
    "Cycle-Canceling": "#2563eb",
    "Capacity-Scaling": "#ea580c"
}


def save_plot(filename):
    """Save the current figure to the plots directory."""
    plt.tight_layout()
    plt.savefig(
        os.path.join(PLOT_DIR, filename),
        dpi=300,
        bbox_inches="tight"
    )
    plt.close()
    print(f"Saved: {filename}")


def plot_execution_time(df):
    """Plot mean execution time with standard deviation."""
    fig, ax = plt.subplots(figsize=(10, 6))

    for algo, group in df.groupby("algorithm"):
        group = group.sort_values("nodes")
        ax.errorbar(
            group["nodes"],
            group["time_mean_us"],
            yerr=group["time_stddev_us"],
            label=algo,
            color=COLORS.get(algo),
            marker="o",
            capsize=5,
            linewidth=2
        )

    ax.set_title("Execution Time vs Number of Nodes")
    ax.set_xlabel("Number of Nodes")
    ax.set_ylabel("Execution Time (µs)")
    ax.legend()
    ax.grid(True, linestyle="--", alpha=0.5)
    save_plot("execution_time.png")


def plot_execution_range(df):
    """Plot minimum, mean and maximum execution times."""
    fig, ax = plt.subplots(figsize=(10, 6))

    for algo, group in df.groupby("algorithm"):
        group = group.sort_values("nodes")
        color = COLORS.get(algo)

        ax.plot(
            group["nodes"],
            group["time_mean_us"],
            marker="o",
            label=f"{algo} Mean",
            color=color,
            linewidth=2
        )

        ax.fill_between(
            group["nodes"],
            group["time_min_us"],
            group["time_max_us"],
            color=color,
            alpha=0.15,
            label=f"{algo} Min-Max"
        )

    ax.set_title("Execution Time Range vs Number of Nodes")
    ax.set_xlabel("Number of Nodes")
    ax.set_ylabel("Execution Time (µs)")
    ax.legend()
    ax.grid(True, linestyle="--", alpha=0.5)
    save_plot("execution_time_range.png")


def plot_memory(df):
    """Plot mean memory usage with standard deviation."""
    fig, ax = plt.subplots(figsize=(10, 6))

    for algo, group in df.groupby("algorithm"):
        group = group.sort_values("nodes")

        ax.errorbar(
            group["nodes"],
            group["memory_mean_kb"],
            yerr=group["memory_stddev_kb"],
            label=algo,
            color=COLORS.get(algo),
            marker="o",
            capsize=5,
            linewidth=2
        )

    ax.set_title("Memory Usage vs Number of Nodes")
    ax.set_xlabel("Number of Nodes")
    ax.set_ylabel("Memory Usage (KB)")
    ax.legend()
    ax.grid(True, linestyle="--", alpha=0.5)
    save_plot("memory_usage.png")


def plot_memory_range(df):
    """Plot minimum, mean and maximum memory usage."""
    fig, ax = plt.subplots(figsize=(10, 6))

    for algo, group in df.groupby("algorithm"):
        group = group.sort_values("nodes")
        color = COLORS.get(algo)

        ax.plot(
            group["nodes"],
            group["memory_mean_kb"],
            marker="o",
            label=f"{algo} Mean",
            color=color,
            linewidth=2
        )

        ax.fill_between(
            group["nodes"],
            group["memory_min_kb"],
            group["memory_max_kb"],
            color=color,
            alpha=0.15,
            label=f"{algo} Min-Max"
        )

    ax.set_title("Memory Usage Range vs Number of Nodes")
    ax.set_xlabel("Number of Nodes")
    ax.set_ylabel("Memory Usage (KB)")
    ax.legend()
    ax.grid(True, linestyle="--", alpha=0.5)
    save_plot("memory_usage_range.png")

def plot_operations(df):
    """Plot all relevant operations in one graph per algorithm."""

    algorithm_metrics = {
        "Cycle-Canceling": [
            ("feasibility_bfs_phases_mean", "Feasibility BFS Phases"),
            ("feasibility_augmentations_mean", "Feasibility Augmentations"),
            ("feasibility_edge_scans_mean", "Feasibility Edge Scans"),
            ("negative_cycle_searches_mean", "Negative Cycle Searches"),
            ("negative_cycle_relaxations_mean", "Negative Cycle Relaxations"),
            ("negative_cycle_edge_scans_mean", "Negative Cycle Edge Scans"),
            ("cycle_cancellations_mean", "Cycle Cancellations"),
            ("cycle_edge_augmentations_mean", "Cycle Edge Augmentations"),
        ],
        "Capacity-Scaling": [
            ("scaling_phases_mean", "Scaling Phases"),
            ("negative_arc_saturations_mean", "Negative Arc Saturations"),
            ("negative_arc_edge_scans_mean", "Negative Arc Edge Scans"),
            ("shortest_path_searches_mean", "Shortest Path Searches"),
            ("shortest_path_successes_mean", "Shortest Path Successes"),
            ("shortest_path_failures_mean", "Shortest Path Failures"),
            ("shortest_path_edge_scans_mean", "Shortest Path Edge Scans"),
            ("dijkstra_relaxations_mean", "Dijkstra Relaxations"),
            ("heap_insert_or_decrease_mean", "Heap Insert/Decrease"),
            ("heap_pops_mean", "Heap Pops"),
            ("augmentations_mean", "Augmentations"),
            ("path_edge_augmentations_mean", "Path Edge Augmentations"),
        ]
    }

    for algo, metrics in algorithm_metrics.items():
        group = df[df["algorithm"] == algo].copy()

        if group.empty:
            print(f"Skipping operations plot for {algo}: no data.")
            continue

        # Average duplicate benchmark records for each dataset/node pair.
        group = (
            group.groupby(["dataset", "nodes"], as_index=False)[
                [column for column, _ in metrics]
            ]
            .mean()
        )

        # Aggregate datasets sharing the same node count.
        group = (
            group.groupby("nodes", as_index=False)[
                [column for column, _ in metrics]
            ]
            .mean()
            .sort_values("nodes")
        )

        fig, ax = plt.subplots(figsize=(14, 8))

        for column, title in metrics:
            # Skip metrics that are zero for all observations.
            if group[column].fillna(0).eq(0).all():
                continue

            ax.plot(
                group["nodes"],
                group[column],
                marker="o",
                linewidth=2,
                markersize=5,
                label=title
            )

        ax.set_title(f"{algo}: Operation Counts vs Number of Nodes")
        ax.set_xlabel("Number of Nodes")
        ax.set_ylabel("Mean Operation Count")
        ax.legend(
            loc="upper left",
            bbox_to_anchor=(1.02, 1),
            fontsize=9
        )
        ax.grid(True, linestyle="--", alpha=0.5)

        filename = (
            "cycle_canceling_operations.png"
            if algo == "Cycle-Canceling"
            else "capacity_scaling_operations.png"
        )

        fig.tight_layout()
        fig.savefig(
            os.path.join(PLOT_DIR, filename),
            dpi=300,
            bbox_inches="tight"
        )
        plt.close(fig)

        print(f"Saved: {filename}")

def plot_time_vs_edges(df):
    """Plot execution time against number of edges."""
    fig, ax = plt.subplots(figsize=(10, 6))

    for algo, group in df.groupby("algorithm"):
        group = group.sort_values("edges")

        ax.plot(
            group["edges"],
            group["time_mean_us"],
            marker="o",
            label=algo,
            color=COLORS.get(algo),
            linewidth=2
        )

    ax.set_title("Execution Time vs Number of Edges")
    ax.set_xlabel("Number of Edges")
    ax.set_ylabel("Execution Time (µs)")
    ax.legend()
    ax.grid(True, linestyle="--", alpha=0.5)
    save_plot("time_vs_edges.png")


def plot_speedup(df):
    """
    Plot relative execution time:
    Capacity-Scaling / Cycle-Canceling.

    A ratio > 1 means Cycle-Canceling is faster.
    A ratio < 1 means Capacity-Scaling is faster.
    """
    pivot = df.pivot_table(
        index=["dataset", "nodes"],
        columns="algorithm",
        values="time_mean_us",
        aggfunc="mean"
    )

    if (
        "Cycle-Canceling" not in pivot.columns
        or "Capacity-Scaling" not in pivot.columns
    ):
        print("Skipping relative execution time plot: "
              "both algorithms are required.")
        return

    pivot = pivot.dropna(
        subset=["Cycle-Canceling", "Capacity-Scaling"]
    )

    pivot = pivot[
        (pivot["Cycle-Canceling"] > 0)
        & (pivot["Capacity-Scaling"] > 0)
    ]

    if pivot.empty:
        print("Skipping relative execution time plot: "
              "no matching valid datasets.")
        return

    pivot["time_ratio"] = (
        pivot["Cycle-Canceling"] /
        pivot["Capacity-Scaling"]
    )

    # Average ratios for datasets with the same node count.
    ratios = pivot.groupby(level="nodes")["time_ratio"].mean()

    fig, ax = plt.subplots(figsize=(10, 6))

    ax.plot(
        ratios.index,
        ratios.values,
        marker="o",
        color="#16a34a",
        linewidth=2
    )

    ax.axhline(
        1,
        color="gray",
        linestyle="--",
        label="Equal execution time"
    )

    ax.set_title(
        "Relative Execution Time: "
        "Cycle-Canceling / Capacity-Scaling"
    )
    ax.set_xlabel("Number of Nodes")
    ax.set_ylabel("Time Ratio")
    ax.legend()
    ax.grid(True, linestyle="--", alpha=0.5)

    save_plot("relative_execution_time.png")


def plot_total_cost(df):
    """Plot total cost for each algorithm."""
    fig, ax = plt.subplots(figsize=(10, 6))

    for algo, group in df.groupby("algorithm"):
        group = group.sort_values("nodes")

        ax.plot(
            group["nodes"],
            group["total_cost"],
            marker="o",
            label=algo,
            color=COLORS.get(algo),
            linewidth=2
        )

    ax.set_title("Total Cost vs Number of Nodes")
    ax.set_xlabel("Number of Nodes")
    ax.set_ylabel("Total Cost")
    ax.legend()
    ax.grid(True, linestyle="--", alpha=0.5)
    save_plot("total_cost.png")


def main():
    parser = argparse.ArgumentParser(
        description="Plot min-cost flow algorithm benchmarks."
    )

    parser.add_argument(
        "csv_file",
        nargs="?",
        default="results_summary.csv",
        help="Path to the benchmark CSV "
             "(default: results_summary.csv)"
    )

    args = parser.parse_args()

    df = pd.read_csv(args.csv_file)

    required = [
        "dataset", "algorithm", "nodes", "edges",
        "total_cost", "run_count",
        "time_mean_us", "time_stddev_us",
        "time_min_us", "time_max_us",
        "memory_mean_kb", "memory_stddev_kb",
        "memory_min_kb", "memory_max_kb",
        "feasibility_bfs_phases_mean",
        "feasibility_augmentations_mean",
        "feasibility_edge_scans_mean",
        "negative_cycle_searches_mean",
        "negative_cycle_relaxations_mean",
        "negative_cycle_edge_scans_mean",
        "cycle_cancellations_mean",
        "cycle_edge_augmentations_mean",
        "scaling_phases_mean",
        "negative_arc_saturations_mean",
        "negative_arc_edge_scans_mean",
        "shortest_path_searches_mean",
        "shortest_path_successes_mean",
        "shortest_path_failures_mean",
        "shortest_path_edge_scans_mean",
        "dijkstra_relaxations_mean",
        "heap_insert_or_decrease_mean",
        "heap_pops_mean",
        "augmentations_mean",
        "path_edge_augmentations_mean"
    ]

    missing = [col for col in required if col not in df.columns]

    if missing:
        raise ValueError(f"Missing CSV columns: {missing}")

    df = df.dropna(
        subset=["dataset", "algorithm", "nodes", "edges"]
    ).copy()

    numeric_columns = [
        col for col in required
        if col not in ["dataset", "algorithm"]
    ]

    for col in numeric_columns:
        df[col] = pd.to_numeric(df[col], errors="raise")

    # Plot benchmark metrics.
    plot_execution_time(df)
    plot_execution_range(df)
    plot_memory(df)
    plot_memory_range(df)
    plot_operations(df)
    plot_time_vs_edges(df)
    plot_speedup(df)
    plot_total_cost(df)

    print(f"\nAll plots saved in '{PLOT_DIR}/'.")


if __name__ == "__main__":
    main()