import os
import argparse
import pandas as pd
import matplotlib.pyplot as plt

# Configuration
PLOT_DIR = "plots"
os.makedirs(PLOT_DIR, exist_ok=True)

COLORS = {
    "Dinic": "#2563eb",
    "Push-Relabel": "#ea580c"
}

def save_plot(filename):
    """Save the current figure to the plots directory."""
    plt.tight_layout()
    plt.savefig(os.path.join(PLOT_DIR, filename), dpi=300,
                bbox_inches="tight")
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

        ax.plot(group["nodes"], group["time_mean_us"],
                marker="o", label=f"{algo} Mean", color=color,
                linewidth=2)

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

        ax.plot(group["nodes"], group["memory_mean_kb"],
                marker="o", label=f"{algo} Mean", color=color,
                linewidth=2)

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
    """Plot algorithm-specific operation counts."""
    metrics = [
        ("bfs_phases_mean", "BFS Phases"),
        ("dfs_calls_mean", "DFS Calls"),
        ("augmentations_mean", "Augmentations"),
        ("push_operations_mean", "Push Operations"),
        ("relabel_operations_mean", "Relabel Operations"),
        ("discharge_operations_mean", "Discharge Operations"),
        ("edge_scans_mean", "Edge Scans"),
        ("global_relabels_mean", "Global Relabels"),
        ("gap_operations_mean", "Gap Operations")
    ]

    fig, axes = plt.subplots(3, 3, figsize=(16, 13))
    axes = axes.flatten()

    for ax, (column, title) in zip(axes, metrics):
        for algo, group in df.groupby("algorithm"):
            group = group.sort_values("nodes")
            ax.plot(
                group["nodes"],
                group[column],
                marker="o",
                label=algo,
                color=COLORS.get(algo),
                linewidth=2
            )

        ax.set_title(title)
        ax.set_xlabel("Nodes")
        ax.set_ylabel("Count")
        ax.grid(True, linestyle="--", alpha=0.5)
        ax.legend(fontsize=8)

    fig.suptitle("Algorithm Operations vs Number of Nodes",
                 fontsize=16)
    fig.tight_layout()
    fig.savefig(os.path.join(PLOT_DIR, "algorithm_operations.png"),
                dpi=300, bbox_inches="tight")
    plt.close(fig)
    print("Saved: algorithm_operations.png")


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
    """Plot relative execution time, handling duplicate node counts."""
    # Match algorithms by dataset and node count, and average
    # duplicate benchmark records if they exist.
    pivot = df.pivot_table(
        index=["dataset", "nodes"],
        columns="algorithm",
        values="time_mean_us",
        aggfunc="mean"
    )

    if "Dinic" not in pivot.columns or "Push-Relabel" not in pivot.columns:
        print("Skipping speedup plot: both algorithms are required.")
        return

    pivot = pivot.dropna(subset=["Dinic", "Push-Relabel"])
    pivot = pivot[pivot["Dinic"] > 0]

    if pivot.empty:
        print("Skipping speedup plot: no matching valid datasets.")
        return

    # Ratio > 1 means Push-Relabel took longer.
    pivot["speedup"] = pivot["Push-Relabel"] / pivot["Dinic"]

    # Average ratios for datasets with the same node count.
    speedup = pivot.groupby(level="nodes")["speedup"].mean()

    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(
        speedup.index,
        speedup.values,
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

    ax.set_title("Relative Execution Time: Push-Relabel / Dinic")
    ax.set_xlabel("Number of Nodes")
    ax.set_ylabel("Time Ratio")
    ax.legend()
    ax.grid(True, linestyle="--", alpha=0.5)

    save_plot("relative_execution_time.png")


def main():
    parser = argparse.ArgumentParser(
        description="Plot max-flow algorithm benchmark results."
    )
    parser.add_argument(
        "csv_file",
        nargs="?",
        default="results.csv",
        help="Path to the benchmark CSV (default: results.csv)"
    )
    args = parser.parse_args()

    df = pd.read_csv(args.csv_file)

    required = [
        "algorithm", "nodes", "edges",
        "time_mean_us", "time_stddev_us",
        "time_min_us", "time_max_us",
        "memory_mean_kb", "memory_stddev_kb",
        "memory_min_kb", "memory_max_kb",
        "bfs_phases_mean", "dfs_calls_mean",
        "augmentations_mean", "push_operations_mean",
        "relabel_operations_mean", "discharge_operations_mean",
        "edge_scans_mean", "global_relabels_mean",
        "gap_operations_mean"
    ]

    missing = [col for col in required if col not in df.columns]
    if missing:
        raise ValueError(f"Missing CSV columns: {missing}")

    df = df.dropna(subset=["algorithm", "nodes", "edges"])
    df["nodes"] = pd.to_numeric(df["nodes"], errors="raise")
    df["edges"] = pd.to_numeric(df["edges"], errors="raise")

    plot_execution_time(df)
    plot_execution_range(df)
    plot_memory(df)
    plot_memory_range(df)
    plot_operations(df)
    plot_time_vs_edges(df)
    plot_speedup(df)

    print(f"\nAll plots saved in '{PLOT_DIR}/'.")


if __name__ == "__main__":
    main()