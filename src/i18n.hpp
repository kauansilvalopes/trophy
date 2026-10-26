// Trophy Manager - interface texts. The source text is English; Portuguese and Spanish are looked up by it.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstring>

namespace trophy::i18n
{
inline int language = 0; // 0 English, 1 Portuguese, 2 Spanish

struct Entry
{
    const char *en, *pt, *es;
};

inline constexpr Entry table[] = {
    {"Your games", "Seus jogos", "Tus juegos"},
    {"Trophies", "Troféus", "Trofeos"},
    {"Locked", "Bloqueado", "Bloqueado"},
    {"Select", "Selecionar", "Seleccionar"},
    {"Open", "Abrir", "Abrir"},
    {"Back", "Voltar", "Volver"},
    {"Refresh", "Atualizar", "Actualizar"},
    {"Menu", "Menu", "Menú"},
    {"Unlock", "Desbloquear", "Desbloquear"},
    {"Lock", "Bloquear", "Bloquear"},
    {"Unlock all", "Desbloquear todos", "Desbloquear todos"},
    {"Lock all", "Bloquear todos", "Bloquear todos"},
    {"Restore original", "Restaurar o original", "Restaurar el original"},
    {"Cancel", "Cancelar", "Cancelar"},
    {"Confirm", "Confirmar", "Confirmar"},
    {"Working...", "Trabalhando...", "Trabajando..."},
    {"OK", "OK", "OK"},
    {"PSN blocked", "PSN bloqueada", "PSN bloqueada"},
    {"PSN reachable: editing is off", "PSN acessível: edição desligada", "PSN accesible: edición desactivada"},
    {"Checking the console...", "Verificando o console...", "Comprobando la consola..."},
    {"Loading your games...", "Carregando seus jogos...", "Cargando tus juegos..."},
    {"No games with trophies were found.", "Nenhum jogo com troféus foi encontrado.",
     "No se encontraron juegos con trofeos."},
    {"Could not reach Elf Arsenal. Start it and press Refresh.",
     "Não foi possível falar com o Elf Arsenal. Inicie-o e atualize.",
     "No se pudo contactar con Elf Arsenal. Inícialo y actualiza."},
    {"Could not reach Elf Arsenal.", "Não foi possível falar com o Elf Arsenal.", "No se pudo contactar con Elf Arsenal."},
    {"Trophy Manager needs Elf Arsenal, a separate homebrew, running on this console.",
     "O Trophy Manager precisa do Elf Arsenal, outro homebrew, rodando neste console.",
     "Trophy Manager necesita Elf Arsenal, otro homebrew, en ejecución en esta consola."},
    {"Start Elf Arsenal, then press Square to refresh.", "Inicie o Elf Arsenal e aperte Quadrado para atualizar.",
     "Inicia Elf Arsenal y pulsa Cuadrado para actualizar."},
    {"Loading trophies...", "Carregando troféus...", "Cargando trofeos..."},
    {"Could not read the trophy file.", "Não foi possível ler o arquivo de troféus.",
     "No se pudo leer el archivo de trofeos."},
    {"Close the running game first.", "Feche o jogo em execução primeiro.", "Cierra primero el juego en ejecución."},
    {"Editing needs the PSN block to be on.", "Editar exige o bloqueio da PSN ligado.",
     "Editar requiere el bloqueo de la PSN activado."},
    {"The console clock is not set.", "O relógio do console não está ajustado.", "El reloj de la consola no está ajustado."},
    {"The trophy file is not valid.", "O arquivo de troféus não é válido.", "El archivo de trofeos no es válido."},
    {"Nothing to change.", "Nada a alterar.", "Nada que cambiar."},
    {"Could not save. Your copy is safe in /data/trophy-manager.",
     "Não foi possível salvar. Sua cópia está segura em /data/trophy-manager.",
     "No se pudo guardar. Tu copia está a salvo en /data/trophy-manager."},
    {"Saved. Open the PS5 trophy list to check.", "Salvo. Abra a lista de troféus do PS5 para conferir.",
     "Guardado. Abre la lista de trofeos de la PS5 para comprobar."},
    {"There is no original copy for this game yet.", "Este jogo ainda não tem cópia original.",
     "Este juego aún no tiene copia original."},
    {"Unlock this trophy?", "Desbloquear este troféu?", "¿Desbloquear este trofeo?"},
    {"Lock this trophy again?", "Bloquear este troféu de novo?", "¿Bloquear este trofeo otra vez?"},
    {"Unlock every trophy of this game, the platinum last?",
     "Desbloquear todos os troféus deste jogo, a platina por último?",
     "¿Desbloquear todos los trofeos de este juego, el platino al final?"},
    {"Lock every trophy of this game?", "Bloquear todos os troféus deste jogo?",
     "¿Bloquear todos los trofeos de este juego?"},
    {"Put back the first copy saved before any edit?", "Colocar de volta a primeira cópia salva antes de qualquer edição?",
     "¿Volver a poner la primera copia guardada antes de cualquier edición?"},
    {"A copy of the file is saved first.", "Uma cópia do arquivo é salva antes.",
     "Antes se guarda una copia del archivo."},
    {"Platinum", "Platina", "Platino"},
    {"Gold", "Ouro", "Oro"},
    {"Silver", "Prata", "Plata"},
    {"Bronze", "Bronze", "Bronce"},
    {"Trophy", "Troféu", "Trofeo"},
    {"earned", "obtidos", "obtenidos"},
    {"Edits stay on this console and never reach PSN.", "As edições ficam neste console e nunca chegam à PSN.",
     "Las ediciones se quedan en esta consola y nunca llegan a la PSN."},
    {"Page", "Página", "Página"},
    {"X  Open     Square  Refresh     L2 / R2  Page     L1  Language",
     "X  Abrir     Quadrado  Atualizar     L2 / R2  Página     L1  Idioma",
     "X  Abrir     Cuadrado  Actualizar     L2 / R2  Página     L1  Idioma"},
    {"X  Unlock / Lock     Square  Select     R1  All     Triangle  Menu     O  Back     L1  Language",
     "X  Desbloquear / Bloquear     Quadrado  Marcar     R1  Todos     Triângulo  Menu     O  Voltar     L1  Idioma",
     "X  Desbloquear / Bloquear     Cuadrado  Marcar     R1  Todos     Triángulo  Menú     O  Volver     L1  Idioma"},
    {"selected", "selecionados", "seleccionados"},
    {"Unlock selected", "Desbloquear selecionados", "Desbloquear seleccionados"},
    {"Lock selected", "Bloquear selecionados", "Bloquear seleccionados"},
    {"Select all", "Selecionar todos", "Seleccionar todos"},
    {"Clear selection", "Limpar seleção", "Borrar selección"},
    {"Unlock the selected trophies?", "Desbloquear os troféus selecionados?",
     "¿Desbloquear los trofeos seleccionados?"},
    {"Lock the selected trophies?", "Bloquear os troféus selecionados?", "¿Bloquear los trofeos seleccionados?"},
    {"No trophies selected.", "Nenhum troféu selecionado.", "Ningún trofeo seleccionado."},
    {"X  Confirm        O  Cancel", "X  Confirmar        O  Cancelar", "X  Confirmar        O  Cancelar"},
    {"Trophy unlocked", "Troféu desbloqueado", "Trofeo desbloqueado"},
    {"Platinum trophy unlocked!", "Troféu de platina desbloqueado!", "¡Trofeo de platino desbloqueado!"},
    {"trophies unlocked", "troféus desbloqueados", "trofeos desbloqueados"},
};

inline const char *tr(const char *en)
{
    if (language == 0 || !en)
        return en;
    for (const Entry &e : table)
        if (!std::strcmp(e.en, en))
            return language == 1 ? e.pt : e.es;
    return en;
}
} // namespace trophy::i18n
