#!/usr/bin/env bash

# Cores no formato hexadecimal. O 33wm passa os valores do arquivo de configuração
# por variáveis de ambiente; sem elas, valem os padrões abaixo (Tokyo Night).
BG_MAIN="${BAR_BG:-#1a1b26}"                    # Fundo principal
FG_MAIN="${BAR_FG:-#a9b1d6}"                    # Texto principal
WS_ACTIVE_BG="${WS_ACTIVE_BG:-#7aa2f7}"         # Fundo do workspace ativo
WS_ACTIVE_FG="${WS_ACTIVE_FG:-#15161e}"         # Texto do workspace ativo
WS_INACTIVE_BG="${WS_INACTIVE_BG:-#24283b}"     # Fundo dos workspaces inativos
ACCENT="${BAR_ACCENT:-#bb9af7}"                 # Cor de destaque

# Os coletores gravam o resultado em variáveis (sem $(...)) e leem /proc e /sys direto.
# Cada $(...) ou comando externo vira um fork a cada segundo, então só sobra o necessário.

# 1. Coleta a memória RAM usada/total em GB (uma casa decimal, em aritmética inteira)
get_ram() {
    local key val total=0 avail=0
    while read -r key val _; do
        case "$key" in
            MemTotal:) total=$val ;;
            MemAvailable:) avail=$val ;;
        esac
    done < /proc/meminfo

    # Décimos de GB arredondados (como o %.1f do awk que antes fazia isso)
    local used_t=$(( ((total - avail) * 10 + 524288) / 1048576 ))
    local total_t=$(( (total * 10 + 524288) / 1048576 ))
    RAM="$((used_t / 10)).$((used_t % 10))GB/$((total_t / 10)).$((total_t % 10))GB"
}

# Último estado lido de /proc/stat: o uso sai da diferença entre dois ciclos do laço
cpu_prev_idle=0
cpu_prev_total=0

# 2. Coleta o uso do processador (CPU %), do sistema inteiro
get_cpu() {
    local _ user nice system idle iowait irq softirq steal
    read -r _ user nice system idle iowait irq softirq steal _ < /proc/stat
    local idle_now=$((idle + iowait))
    local total_now=$((user + nice + system + idle + iowait + irq + softirq + steal))

    local diff_idle=$((idle_now - cpu_prev_idle))
    local diff_total=$((total_now - cpu_prev_total))
    cpu_prev_idle=$idle_now
    cpu_prev_total=$total_now

    CPU=0
    if [ "$diff_total" -gt 0 ]; then
        CPU=$(( (100 * (diff_total - diff_idle)) / diff_total ))
    fi
    CPU+="%"
}

# 3. Coleta a porcentagem e status da Bateria
get_battery() {
    local bat_path="/sys/class/power_supply/BAT0"
    [ ! -d "$bat_path" ] && bat_path="/sys/class/power_supply/BAT1"

    if [ -d "$bat_path" ]; then
        local cap=0 status=Discharging
        read -r cap 2>/dev/null < "$bat_path/capacity" || cap=0
        read -r status 2>/dev/null < "$bat_path/status" || status=Discharging
        if [ "$status" = "Charging" ]; then
            BAT="⚡ ${cap}%"
        else
            BAT="🔋 ${cap}%"
        fi
    else
        BAT="💻 AC"
    fi
}

# Nome da rede Wi-Fi, consultado a cada 10 ciclos (o SSID muda raramente)
net_ssid="Wi-Fi"
net_ssid_ttl=0

# 4. Coleta a Rede Conectada (Wi-Fi SSID ou Cabo). A interface vem da rota padrão em
# /proc/net/route (destino 00000000)
get_net() {
    local iface="" name dest _rest
    while read -r name dest _rest; do
        if [ "$dest" = "00000000" ]; then
            iface=$name
            break
        fi
    done < /proc/net/route

    if [ -z "$iface" ]; then
        NET="❌ Off"
        return
    fi

    if [[ "$iface" == w* ]]; then
        if [ "$net_ssid_ttl" -le 0 ]; then
            net_ssid=$(iwgetid -r 2>/dev/null)
            [ -z "$net_ssid" ] && net_ssid="Wi-Fi"
            net_ssid_ttl=10
        fi
        net_ssid_ttl=$((net_ssid_ttl - 1))
        NET="📶 $net_ssid"
    else
        NET="🔌 Cabo ($iface)"
    fi
}

# 5. Formatação da Data e Hora (o printf do bash formata sem chamar o comando date)
get_clock() {
    printf -v CLOCK "📅 %(%d/%m/%Y %H:%M)T" -1
}

# 6. Renderização dos Workspaces (0 a 8 -> 1 a 9) com Ação de Clique
get_workspaces() {
    local line curr_ws=0 occupied=0

    # Uma só consulta ao X para as duas propriedades
    while read -r line; do
        case "$line" in
            _NET_CURRENT_DESKTOP*) curr_ws=${line##* } ;;
            _33WM_OCCUPIED*) occupied=${line##* } ;;
        esac
    done < <(xprop -root _NET_CURRENT_DESKTOP _33WM_OCCUPIED 2>/dev/null)

    # Máscara de workspaces com janelas, publicada pelo WM (bit i = workspace i)
    [[ "$curr_ws" =~ ^[0-9]+$ ]] || curr_ws=0
    [[ "$occupied" =~ ^[0-9]+$ ]] || occupied=0

    WORKSPACES=""
    for i in {0..8}; do
        # Mostra só os workspaces com janelas e o ativo (dinâmico, como no niri)
        if [ "$i" -ne "$curr_ws" ] && [ $(( (occupied >> i) & 1 )) -eq 0 ]; then
            continue
        fi
        ws_num=$((i + 1))
        # %{A1:xdotool set_desktop $i:} torna a área clicável (botão esquerdo = ação)
        if [ "$i" -eq "$curr_ws" ]; then
            WORKSPACES+="%{A1:xdotool set_desktop $i:}%{B$WS_ACTIVE_BG}%{F$WS_ACTIVE_FG} $ws_num %{F-}%{B-}%{A} "
        else
            WORKSPACES+="%{A1:xdotool set_desktop $i:}%{B$WS_INACTIVE_BG}%{F$FG_MAIN} $ws_num %{F-}%{B-}%{A} "
        fi
    done
}

# Primeira leitura de /proc/stat: o próximo ciclo já tem um intervalo para calcular o uso
get_cpu
sleep 1

# Loop Principal de Atualização
while true; do
    get_workspaces
    get_ram
    get_cpu
    get_battery
    get_net
    get_clock

    # Sintaxe do Lemonbar:
    # %{l} = Alinhado à esquerda | %{c} = Alinhado ao centro | %{r} = Alinhado à direita
    echo "%{l} ${WORKSPACES} %{r} %{F$ACCENT}RAM:%{F-} ${RAM} | %{F$ACCENT}CPU:%{F-} ${CPU} | ${BAT} | ${NET} | ${CLOCK} "

    sleep 1
done
