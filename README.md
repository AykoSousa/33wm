# 33wm

Gerenciador de janelas em mosaico com colunas rolantes (inspirado no niri), para X11, escrito em C com XCB.

Tem barra própria (lemonbar + `bar.sh`), workspaces dinâmicos, janelas flutuantes para diálogos, bordas no estilo i3 e configuração por arquivo.

## Dependências (Void Linux)

Compilação:

```sh
sudo xbps-install -S gcc make pkg-config libxcb-devel xcb-util-keysyms-devel libX11-devel xorgproto
```

Execução:

```sh
sudo xbps-install -S xorg-server xinit lemonbar-xft xdotool xprop iproute2 wireless_tools rofi konsole \
  pulseaudio-utils brightnessctl playerctl
```

- `lemonbar-xft`: a barra (o `lemonbar` comum não renderiza bem os emojis do `bar.sh`).
- `xdotool`: cliques nos workspaces da barra.
- `xprop`: lido pela barra para os workspaces.
- `iproute2` (`ip`) e `wireless_tools` (`iwgetid`): rede na barra.
- `rofi`: launcher padrão. `konsole`: terminal padrão. Ambos são configuráveis.
- `pulseaudio-utils` (`pactl`), `brightnessctl` e `playerctl`: comandos padrão das teclas de mídia.
- `xorg-server-xephyr` é opcional, para testar o WM numa janela.

## Compilar e instalar

```sh
make                              # gera ./33wm
sudo make install                 # instala em /usr/local (bin/33wm e share/33wm/bar.sh)
make PREFIX=$HOME/.local install  # instalação no diretório do usuário
```

`make clean` remove os objetos e o binário.

## Iniciar

Pelo `startx`, com um `~/.xinitrc`:

```sh
exec /usr/local/bin/33wm
```

Para testar sem sair da sessão atual, num Xephyr:

```sh
Xephyr :1 -screen 1280x800 -ac &
DISPLAY=:1 ./33wm
```

## Sessão (login)

Com `sudo make install`, o 33wm aparece como sessão "33wm" no gerenciador de login (SDDM, LightDM...), pelo arquivo `/usr/local/share/xsessions/33wm.desktop`.

Para sair da sessão pelo terminal:

```sh
pkill -x 33wm
```

O WM encerra com limpeza (a barra é encerrada junto) e a sessão X termina, voltando ao login. Se o terminal estiver dentro da própria sessão, funciona do mesmo jeito. Se a sessão travar, use Ctrl+Alt+F2 para abrir outro terminal e rode o mesmo comando.

## Atalhos

O modificador padrão é Super (`mod = alt` na configuração troca para Alt).

| Atalho | Ação |
| --- | --- |
| Super + H / L | Foco na coluna à esquerda / direita |
| Super + J / K | Foco na janela abaixo / acima da coluna |
| Super + V | Puxa a próxima coluna para a ativa |
| Super + Shift + V | Expulsa a janela ativa para uma nova coluna |
| Super + - / = | Diminui / aumenta a largura da coluna (`resize_step`) |
| Super + M | Maximiza a coluna ativa |
| Super + F | Tela cheia da janela ativa |
| Super + Enter | Abre o terminal |
| Super + Space | Abre o launcher |
| Super + Q | Fecha a janela. Se ela não fechar, Super + Q de novo a mata |
| Super + 1..9 | Vai para o workspace (se ele já está em outro monitor, o foco vai para lá) |
| Super + Shift + 1..9 | Move a coluna ativa para o workspace |
| Super + , / . | Foco no monitor anterior / seguinte |
| Super + Shift + , / . | Move a coluna ativa para o workspace do monitor anterior / seguinte |
| Super + botão 1 (arrastar) | Move uma janela flutuante |
| Super + botão 3 (arrastar) | Redimensiona uma janela flutuante |

Teclas de mídia (sem modificador): volume, mute do som, mute do microfone, brilho, play/pause, próxima e anterior. Cada uma roda o comando configurado.

## Configuração

Arquivos em `$XDG_CONFIG_HOME/33wm/` (ou `~/.config/33wm/`):

- `config`: opções do WM, da barra e das bordas. Veja `config.example`, que lista todas as chaves com os padrões.
- `autostart`: uma linha por comando, executado ao iniciar o WM. Linhas com `#` e vazias são ignoradas.

```sh
mkdir -p ~/.config/33wm
cp config.example ~/.config/33wm/config
printf 'nm-applet\n' > ~/.config/33wm/autostart
```

Valores inválidos ou chaves desconhecidas geram um aviso no stderr e usam o padrão.

Para aplicar mudanças sem reiniciar o WM, envie SIGHUP: `pkill -HUP -x 33wm`. Isso relê a configuração, reaplica as teclas e reinicia a barra. As janelas continuam como estavam.

Para desativar uma tecla de mídia, deixe o comando vazio (ex.: `brightness_up_cmd =`).

## Comportamento

- **Layout:** colunas rolantes. A coluna ativa fica centralizada, sem deixar espaço vazio nas bordas.
- **Barra:** o `bar.sh` roda no lemonbar e mostra só os workspaces com janelas e o ativo. A barra é reiniciada se morrer e é encerrada junto com o WM.
- **Janelas flutuantes:** diálogos, utilitários, splash, toolbars e janelas com `WM_TRANSIENT_FOR` ficam fora do layout, centralizadas no workspace onde abriram.
- **Tamanho mínimo:** o WM respeita `WM_NORMAL_HINTS` (tamanho mínimo e incrementos, como em terminais).
- **EWMH:** o WM publica `_NET_ACTIVE_WINDOW`, `_NET_CLIENT_LIST`, `_NET_CURRENT_DESKTOP` e o estado de tela cheia pedido pelos apps (`_NET_WM_STATE_FULLSCREEN`).

## Monitores

Os monitores vêm do RandR (monitores do X). O primeiro da lista (o principal) leva a barra e o `top_padding`. Cada workspace fica visível em, no máximo, um monitor, e cada monitor mostra um workspace.

Com mais de um monitor, uma coluna que não cabe inteira no monitor (cortada pela borda) fica escondida, para não aparecer por cima do monitor vizinho. A coluna ativa sempre aparece.

## Limitações conhecidas

- Workspaces vazios não aparecem na barra, então não dá para clicar neles. Use Super + número.
- Sem timeout para forçar o fechamento: o segundo Super + Q é o que força.
- O mouse só move e redimensiona janelas flutuantes. Janelas em coluna não são arrastadas, e não há foco por clique.
- A detecção de monitores e a troca de monitor foram testadas com monitores simulados (no Xephyr, que não oferece dois monitores reais). Conectar ou remover um monitor com o WM rodando não foi testado de verdade.

## Testes

Os arquivos `test_*.c` são programas antigos, fora da compilação. Os testes desta versão foram manuais, no Xephyr.
