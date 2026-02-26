import pvlib
import numpy as np

# --- Panel setup (do once) ---
cec_modules = pvlib.pvsystem.retrieve_sam('CECMod')
module = cec_modules['Canadian_Solar_Inc__CS6U_330P']

def get_panel_current(voltage, irradiance=1000, temp_cell=25):
    """Simulate the panel: given a voltage, return current."""
    params = pvlib.pvsystem.calcparams_cec(
        effective_irradiance=irradiance,
        temp_cell=temp_cell,
        alpha_sc=module['alpha_sc'],
        a_ref=module['a_ref'],
        I_L_ref=module['I_L_ref'],
        I_o_ref=module['I_o_ref'],
        R_sh_ref=module['R_sh_ref'],
        R_s=module['R_s'],
        Adjust=module['Adjust'],
    )
    current = pvlib.pvsystem.i_from_v(
        voltage=voltage,
        photocurrent=params[0],
        saturation_current=params[1],
        resistance_series=params[2],
        resistance_shunt=params[3],
        nNsVth=params[4],
    )
    return max(current, 0)


# --- Perturb & Observe MPPT ---
def mppt_perturb_and_observe(steps=100, v_start=20.0, dv=0.5, irradiance=1000):
    """Simple P&O MPPT against the pvlib panel model."""
    v = v_start
    i = get_panel_current(v, irradiance)
    p = v * i
    
    history = {'v': [], 'i': [], 'p': []}
    direction = 1  # +1 or -1
    
    for _ in range(steps):
        history['v'].append(v)
        history['i'].append(i)
        history['p'].append(p)
        
        # Perturb
        v_new = v + direction * dv
        i_new = get_panel_current(v_new, irradiance)
        p_new = v_new * i_new
        
        # Observe
        if p_new > p:
            pass  # keep going same direction
        else:
            direction *= -1  # reverse
        
        print(f"V={v:.1f}V, I={i:.1f}A, P={p:.1f}W, direction={'+' if direction > 0 else '-'}")
        v, i, p = v_new, i_new, p_new
    
    return history

history = mppt_perturb_and_observe()
print(f"Converged to: {max(history['p']):.1f}W at {history['v'][np.argmax(history['p'])]:.1f}V")