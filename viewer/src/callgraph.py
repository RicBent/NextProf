import graphviz
from .profile import Profile


def get_gradient_color(percentage: float, max_percentage: float) -> str:
    """Get a smooth gradient color from grey through yellow, orange to red.
    
    Args:
        percentage: The percentage value (0-100+)
        max_percentage: The percentage at which we reach full red
    
    Returns:
        A hex color string
    """
    # Normalize percentage to 0-1 range
    normalized = min(percentage / max_percentage, 1.0)
    
    # Define color stops: grey -> yellow -> orange -> red
    if normalized < 0.33:
        # Grey to yellow
        t = normalized / 0.33
        r = int(0xbd + (0xf1 - 0xbd) * t)
        g = int(0xc3 + (0xc4 - 0xc3) * t)
        b = int(0xc7 + (0x0f - 0xc7) * t)
    elif normalized < 0.66:
        # Yellow to orange
        t = (normalized - 0.33) / 0.33
        r = int(0xf1 + (0xe6 - 0xf1) * t)
        g = int(0xc4 + (0x7e - 0xc4) * t)
        b = int(0x0f + (0x22 - 0x0f) * t)
    else:
        # Orange to red
        t = (normalized - 0.66) / 0.34
        r = int(0xe6 + (0xe7 - 0xe6) * t)
        g = int(0x7e + (0x4c - 0x7e) * t)
        b = int(0x22 + (0x3c - 0x22) * t)
    
    return f'#{r:02x}{g:02x}{b:02x}'


def generate_callgraph(
    profile: Profile,
    min_percentage: float = 1.0,
    min_edge_percentage: float = 5.0,
    critical_percentage: float = 5.0,
    critical_by_direct: bool = False,
) -> graphviz.Digraph:
    """Generate a gprof2dot-style call graph from profiling data."""

    critical_percentage = max(critical_percentage, 0.001)

    dot = graphviz.Digraph(comment='Call Graph')
    dot.attr(rankdir='TB')
    dot.attr('node', shape='box', style='filled', fontname='monospace')
    dot.attr('edge', fontname='monospace', fontsize='10')
    
    total_hits = sum(f.hit_count for f in profile.funcs)
    if total_hits == 0:
        return dot
    
    significant_funcs = [
        f for f in profile.funcs 
        if (f.hit_count / total_hits * 100) >= min_percentage
    ]
    
    # Add nodes for significant functions
    # TODO: maybe add functions surrounding significant ones to a certain depth? Make that an arg?
    func_by_addr = {f.address: f for f in significant_funcs}
    for func in significant_funcs:
        percentage = func.hit_count / total_hits * 100
        direct_percentage = func.hit_count_direct / total_hits * 100 if func.hit_count_direct else 0

        color = get_gradient_color(direct_percentage if critical_by_direct else percentage, critical_percentage)

        func_name = func.name
        if len(func_name) > 50:
            func_name = func_name[:47] + '...'
        
        label = f'{func_name}\\n{percentage:.1f}% ({func.hit_count})'
        if func.hit_count_direct > 0:
            label += f'\\n{direct_percentage:.1f}% direct ({func.hit_count_direct})'
        
        dot.node(
            f'func_{func.address:x}',
            label=label,
            fillcolor=color
        )
    
    # Add edges for call relationships
    # Percentages: call count / total hits
    for func in significant_funcs:
        for callee_addr, call_count in func.callees.items():
            if not callee_addr in func_by_addr:
                continue
            edge_percentage = call_count / func.hit_count * 100
            if edge_percentage < min_edge_percentage:
                continue
            dot.edge(
                f'func_{func.address:x}',
                f'func_{callee_addr:x}',
                label=f'{edge_percentage:.1f}%'
            )
    
    return dot
