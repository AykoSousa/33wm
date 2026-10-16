#!/usr/bin/env bash

# Cores no formato hexadecimal. O 33wm passa os valores do arquivo de configuração
# por variáveis de ambiente; sem elas, valem os padrões abaixo (Tokyo Night).
BG_MAIN="${BAR_BG:-#1a1b26}"                    # Fundo principal
FG_MAIN="${BAR_FG:-#a9b1d6}"                    # Texto principal
WS_ACTIVE_BG="${WS_ACTIVE_BG:-#7aa2f7}"         # Fundo do workspace ativo
WS_ACTIVE_FG="${WS_ACTIVE_FG:-#15161e}"         # Texto do workspace ativo
WS_INACTIVE_BG="${WS_INACTIVE_BG:-#24283b}"     # Fundo dos workspaces inativos
ACCENT="${BAR_ACCENT:-#bb9af7}"                 # Cor de destaque

# 1. Coleta a memória RAM usada/total em GB
get_ram() {
    while read -r key val _; do
        case "$key" in
            MemTotal:) total=$val ;;
            MemAvailable:) avail=$val ;;
        esac
    done < /proc/meminfo

    used_kb=$((total - avail))
    used_gb=$(awk "BEGIN {printf \"%.1f\", $used_kb/1048576}")
    total_gb=$(awk "BEGIN {printf \"%.1f\", $total/1048576}")
    echo "${used_gb}GB/${total_gb}GB"
}

# 2. Coleta o uso do processador (CPU %)
get_cpu() {
    read -r _ user nice system idle iowait irq softirq steal _ < /proc/stat
    prev_idle=$((idle + iowait))
    prev_total=$((user + nice + system + idle + iowait + irq + softirq + steal))

    sleep 0.3

    read -r _ user nice system idle iowait irq softirq steal _ < /proc/stat
    idle=$((idle + iowait))
    total=$((user + nice + system + idle + iowait + irq + softirq + steal))

    diff_idle=$((idle - prev_idle))
    diff_total=$((total - prev_total))

    if [ "$diff_total" -gt 0 ]; then
        cpu_usage=$(( (100 * (diff_total - diff_idle)) / diff_total ))
    else
        cpu_usage=0
    fi
    echo "${cpu_usage}%"
}

# 3. Coleta a porcentagem e status da Bateria
get_battery() {
    bat_path="/sys/class/power_supply/BAT0"
    [ ! -d "$bat_path" ] && bat_path="/sys/class/power_supply/BAT1"

    if [ -d "$bat_path" ]; then
        cap=$(cat "$bat_path/capacity" 2>/dev/null || echo "0")
        status=$(cat "$bat_path/status" 2>/dev/null || echo "Discharging")
        if [ "$status" = "Charging" ]; then
            echo "⚡ ${cap}%"
        else
            echo "🔋 ${cap}%"
        fi
    else
        echo "💻 AC"
    fi
}

# 4. Coleta a Rede Conectada (Wi-Fi SSID ou Cabo)
get_net() {
    iface=$(ip route show default 2>/dev/null | awk '/default/ {print $5}' | head -n1)
    if [ -z "$iface" ]; then
        echo "❌ Off"
        return
    fi

    if [[ "$iface" == w* ]]; then
        ssid=$(iwgetid -r 2>/dev/null)
        [ -z "$ssid" ] && ssid="Wi-Fi"
        echo "📶 $ssid"
    else
        echo "🔌 Cabo ($iface)"
    fi
}

# 5. Formatação da Data e Hora
get_clock() {
    date "+📅 %d/%m/%Y %H:%M"
}

# 6. Renderização dos Workspaces (0 a 8 -> 1 a 9) com Ação de Clique
get_workspaces() {
    curr_ws=$(xprop -root _NET_CURRENT_DESKTOP 2>/dev/null | awk '{print $3}')
    [ -z "$curr_ws" ] && curr_ws=0

    # Máscara de workspaces com janelas, publicada pelo WM (bit i = workspace i)
    occupied=$(xprop -root _33WM_OCCUPIED 2>/dev/null | awk '{print $3}')
    [[ "$occupied" =~ ^[0-9]+$ ]] || occupied=0

    ws_out=""
    for i in {0..8}; do
        # Mostra só os workspaces com janelas e o ativo (dinâmico, como no niri)
        if [ "$i" -ne "$curr_ws" ] && [ $(( (occupied >> i) & 1 )) -eq 0 ]; then
            continue
        fi
        ws_num=$((i + 1))
        # %{A1:xdotool set_desktop $i:} torna a área clicável (botão esquerdo = ação)
        if [ "$i" -eq "$curr_ws" ]; then
            ws_out+="%{A1:xdotool set_desktop $i:}%{B$WS_ACTIVE_BG}%{F$WS_ACTIVE_FG} $ws_num %{F-}%{B-}%{A} "
        else
            ws_out+="%{A1:xdotool set_desktop $i:}%{B$WS_INACTIVE_BG}%{F$FG_MAIN} $ws_num %{F-}%{B-}%{A} "
        fi
    done
    echo "$ws_out"
}

# Loop Principal de Atualização
while true; do
    WORKSPACES=$(get_workspaces)
    RAM=$(get_ram)
    CPU=$(get_cpu)
    BAT=$(get_battery)
    NET=$(get_net)
    CLOCK=$(get_clock)

    # Sintaxe do Lemonbar:
    # %{l} = Alinhado à esquerda | %{c} = Alinhado ao centro | %{r} = Alinhado à direita
    echo "%{l} ${WORKSPACES} %{r} %{F$ACCENT}RAM:%{F-} ${RAM} | %{F$ACCENT}CPU:%{F-} ${CPU} | ${BAT} | ${NET} | ${CLOCK} "

    sleep 1
done