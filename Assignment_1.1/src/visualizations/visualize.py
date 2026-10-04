import matplotlib.pyplot as plt
import networkx as nx
import csv

from networkx.drawing.nx_agraph import graphviz_layout

G = nx.DiGraph()

with open("../../dataset/dataset_200n.txt", "r", buffering=1024 * 1024) as f:
    reader = csv.reader(f, delimiter=" ")

    for row in reader:
        if len(row) >= 4 and row[0] == "a" and row[3] != "0":
            G.add_edge(row[1], row[2])


print(f"Nodes: {G.number_of_nodes():,}")
print(f"Edges: {G.number_of_edges():,}")

pos = graphviz_layout(
    G,
    prog="sfdp",
)

plt.figure(figsize=(20, 14))

fig = plt.gcf()
fig.patch.set_facecolor("black")
plt.gca().set_facecolor("black")

nx.draw_networkx_edges(
    G,
    pos,
    edge_color="gray",
    alpha=0.25,
    width=0.4,
    arrows=False
)

nx.draw_networkx_nodes(
    G,
    pos,
    node_color="white",
    node_size=12,
    alpha=0.9,
    linewidths=0
)

plt.title(
    f"Graph — {G.number_of_nodes():,} nodes, "
    f"{G.number_of_edges():,} edges",
    color="white"
)

plt.axis("off")
plt.tight_layout()
plt.show()